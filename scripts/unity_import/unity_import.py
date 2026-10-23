#!/usr/bin/env python3
"""Converts a Unity project into a Mercury project.

    unity_import.py --unity path/to/UnityProject --out path/to/MercuryProject \\
        [--scripts path/to/script_map.py] [--model-info path/to/mercury_model_info] \\
        [--substitute GUID=Assets/Path/To/Replacement.fbx] [--name "Game"]

What it writes:
  assets/...            every model, texture, sound, font, UXML and USS file,
                        with materials (.masset), prefabs (.mprefab), model
                        prefabs (<model>.mprefab), data assets (.masset),
                        animator controllers (.manim) and render textures
  maps/<Scene>.mmap     each scene under Assets/Scenes
  input/<name>.minput   the Input System actions
  <name>.mproj          layers, tags, physics, input and render settings

Scripts are not translated automatically: a script map (a Python module)
names the C++ class for each MonoBehaviour and converts its fields. See
convert_script() below for the interface.
"""

import argparse
import copy
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.parse

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import convert as cv  # noqa: E402
import mtext as mt  # noqa: E402
import resolve as rs  # noqa: E402
import unity_yaml as uy  # noqa: E402

COPY_EXTENSIONS = {".fbx", ".obj", ".gltf", ".glb", ".png", ".jpg", ".jpeg", ".tga", ".bmp",
                   ".psd", ".wav", ".ogg", ".mp3", ".ttf", ".otf", ".svg", ".blend"}
TEXT_UI_EXTENSIONS = {".uxml", ".uss", ".tss"}


class Context:
    def __init__(self, args):
        self.unity = os.path.abspath(args.unity)
        self.out = os.path.abspath(args.out)
        self.model_info_tool = args.model_info
        self.db = rs.AssetDatabase(self.unity)
        self.resolver = rs.Resolver(self.db, self.model_info)
        self.warnings = []
        self.substitutes = {}
        for item in args.substitute or []:
            guid, path = item.split("=", 1)
            self.substitutes[guid] = path
        self.converted = set()
        self.pending = []
        self._model_info = {}
        self._script_names = {}
        self.script_map = None
        if args.scripts:
            spec = importlib.util.spec_from_file_location("script_map", args.scripts)
            self.script_map = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(self.script_map)
        self.cache_file = os.path.join(self.out, ".mercury", "model_info.json")
        if os.path.exists(self.cache_file):
            with open(self.cache_file) as f:
                self._model_info = json.load(f)

    def warn(self, message):
        if message not in self.warnings:
            self.warnings.append(message)
            print("warning:", message, file=sys.stderr)

    # ---- paths

    def out_rel(self, unity_rel):
        """Where an asset goes in the Mercury project (relative path)."""
        rel = unity_rel
        if rel.startswith("Assets/"):
            rel = "assets/" + rel[len("Assets/"):]
        elif rel.startswith("Packages/"):
            rel = "packages/" + rel[len("Packages/"):]
        base, ext = os.path.splitext(rel)
        lower = ext.lower()
        if lower == ".mat" or lower == ".asset" or lower == ".physicmaterial" or \
                lower == ".rendertexture":
            return base + ".masset"
        if lower == ".prefab":
            return base + ".mprefab"
        if lower == ".controller":
            return base + ".manim"
        if lower == ".inputactions":
            return "input/" + os.path.basename(base) + ".minput"
        return rel

    def res(self, unity_rel):
        return mt.Resource("res://" + self.out_rel(unity_rel))

    def write(self, rel, text):
        path = os.path.join(self.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)

    def substitute(self, guid):
        if guid in self.substitutes:
            path = self.substitutes[guid]
            return self.db.guid_by_path.get(path, guid)
        return guid

    # ---- models

    def model_info(self, abs_path):
        rel = os.path.relpath(abs_path, self.unity)
        if rel in self._model_info:
            return self._model_info[rel]
        if not self.model_info_tool:
            return None
        try:
            output = subprocess.run([self.model_info_tool, "--unity", abs_path],
                                    capture_output=True, text=True, check=True).stdout
            info = json.loads(output)[0]
        except (subprocess.CalledProcessError, json.JSONDecodeError, IndexError) as e:
            self.warn(f"could not inspect {rel}: {e}")
            info = None
        self._model_info[rel] = info
        os.makedirs(os.path.dirname(self.cache_file), exist_ok=True)
        with open(self.cache_file, "w") as f:
            json.dump(self._model_info, f)
        return info

    def default_mesh_node(self, unity_rel):
        """The node whose mesh a MeshFilter of this model shows: Unity's
        model root, which for one-mesh models holds the mesh."""
        info = self.model_info(os.path.join(self.unity, unity_rel)) or {}
        nodes = info.get("nodes") or []
        return nodes[0]["name"] if nodes else ""

    # ---- references

    def asset_ref(self, reference):
        """Resource of a referenced asset (converting it when needed)."""
        file_id, guid = uy.ref(reference)
        if not guid or guid == rs.BUILTIN_GUID:
            return None
        guid = self.substitute(guid)
        rel = self.db.path(guid)
        if rel is None:
            self.warn(f"reference to missing asset {guid}")
            return None
        self.request(rel)
        if rel.lower().endswith((".fbx", ".obj")) and file_id == 100100000:
            return mt.Resource("res://" + self.model_prefab_rel(rel))
        return self.res(rel)

    def material_ref(self, reference):
        if isinstance(reference, dict) and reference.get("_embedded") is not None:
            return None
        return self.asset_ref(reference)

    def prefab_ref(self, reference):
        """Resource of the prefab a reference points into (prefab or model)."""
        file_id, guid = uy.ref(reference)
        if not guid:
            return None
        rel = self.db.path(self.substitute(guid))
        if rel is None:
            return None
        self.request(rel)
        if rel.lower().endswith((".fbx", ".obj")):
            self.request_model_prefab(rel)
            return mt.Resource("res://" + self.model_prefab_rel(rel))
        return self.res(rel)

    def model_prefab_rel(self, rel):
        return self.out_rel(rel) + ".mprefab"

    def request(self, rel):
        if rel not in self.converted:
            self.converted.add(rel)
            self.pending.append(rel)

    def request_model_prefab(self, rel):
        key = rel + "#prefab"
        if key not in self.converted:
            self.converted.add(key)
            self.pending.append(key)

    def text_font(self, reference):
        return None  # TextMesh uses the engine's LiberationSans

    # ---- scripts

    def script_name(self, comp):
        guid = comp.script_guid() if hasattr(comp, "script_guid") else None
        if guid is None:
            return None
        if guid in self._script_names:
            return self._script_names[guid]
        name = None
        rel = self.db.path(guid)
        if rel and rel.endswith(".cs"):
            with open(os.path.join(self.unity, rel), encoding="utf-8", errors="replace") as f:
                text = f.read()
            m = re.search(r"\bclass\s+(\w+)\s*:\s*[\w\.]+", text)
            name = m.group(1) if m else os.path.splitext(os.path.basename(rel))[0]
        elif rel:
            name = os.path.splitext(os.path.basename(rel))[0]
        else:
            # Package scripts: guess from the class identifier Unity stores.
            ident = comp.data.get("m_EditorClassIdentifier") or ""
            name = ident.split("::")[-1].split(".")[-1] or None
        self._script_names[guid] = name
        return name

    def script_type(self, comp):
        name = self.script_name(comp)
        return self.script_type_by_class(name) if name else None

    def script_type_by_class(self, name):
        table = getattr(self.script_map, "SCRIPTS", {}) if self.script_map else {}
        entry = table.get(name)
        if entry is None:
            return None
        return entry[0]

    def convert_script(self, name, comp, converter, node):
        """Script map interface: SCRIPTS = {"UnityClass": ("CppClass", fn)}
        where fn(data, converter, ctx) returns [(field, value), ...]."""
        table = getattr(self.script_map, "SCRIPTS", {}) if self.script_map else {}
        entry = table.get(name)
        if entry is None:
            return None
        type_name, fn = entry
        return type_name, fn(comp.data, converter, self) if fn else []

    # ---- conversion of assets

    def run(self):
        while self.pending:
            rel = self.pending.pop(0)
            if rel.endswith("#prefab"):
                self.convert_model_prefab(rel[:-len("#prefab")])
                continue
            ext = os.path.splitext(rel)[1].lower()
            try:
                if ext == ".mat":
                    self.convert_material(rel)
                elif ext == ".prefab":
                    self.convert_prefab(rel)
                elif ext in (".fbx", ".obj"):
                    self.copy(rel)
                    self.request_model_prefab(rel)
                elif ext == ".asset":
                    self.convert_asset(rel)
                elif ext == ".physicmaterial":
                    self.convert_physic_material(rel)
                elif ext == ".rendertexture":
                    self.convert_render_texture(rel)
                elif ext == ".controller":
                    self.convert_controller(rel)
                elif ext in COPY_EXTENSIONS:
                    self.copy(rel)
                elif ext in TEXT_UI_EXTENSIONS:
                    self.convert_ui_text(rel)
            except Exception as e:  # keep going; report at the end
                import traceback
                traceback.print_exc()
                self.warn(f"failed to convert {rel}: {e}")

    def copy(self, rel):
        src = os.path.join(self.unity, rel)
        dst = os.path.join(self.out, self.out_rel(rel))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src):
            shutil.copy2(src, dst)

    def copy_all(self):
        """Every model, image, sound and font, as the project's own."""
        for guid, rel in sorted(self.db.by_guid.items(), key=lambda x: x[1]):
            if not rel.startswith("Assets/"):
                continue
            ext = os.path.splitext(rel)[1].lower()
            if ext in COPY_EXTENSIONS and os.path.isfile(os.path.join(self.unity, rel)):
                self.copy(rel)
            elif ext in TEXT_UI_EXTENSIONS:
                self.request(rel)

    def convert_ui_text(self, rel):
        with open(os.path.join(self.unity, rel), encoding="utf-8", errors="replace") as f:
            text = f.read()

        def replace(m):
            path = urllib.parse.unquote(m.group(1))
            unity_rel = "Assets/" + path
            ext = os.path.splitext(path)[1].lower()
            if ext in COPY_EXTENSIONS or ext in TEXT_UI_EXTENSIONS:
                self.request(unity_rel)
            return "res://" + self.out_rel(unity_rel)

        # The query (and #name) ends at a quote, which may be written &quot;.
        text = re.sub(r"project://database/Assets/((?:(?!&quot;)[^?\"')#&])*)"
                      r"((?:\?|#)(?:(?!&quot;)[^\"')])*)?", replace, text)
        self.write(self.out_rel(rel), text)

    def convert_material(self, rel):
        objects = uy.parse_file(os.path.join(self.unity, rel))
        mat = next((o.data for o in objects.values() if o.type_name == "Material"), None)
        if mat is None:
            return
        props = mat.get("m_SavedProperties") or {}
        floats = {}
        for item in props.get("m_Floats") or []:
            floats.update(item)
        colors = {}
        for item in props.get("m_Colors") or []:
            colors.update(item)
        textures = {}
        for item in props.get("m_TexEnvs") or []:
            textures.update(item)
        shader_rel = self.db.path(uy.ref(mat.get("m_Shader"))[1] or "") or ""
        keywords = mat.get("m_ValidKeywords") or []
        out = []
        base_color = colors.get("_BaseColor") or colors.get("_Color")
        if base_color:
            out.append(("color", cv.color(base_color)))
        base_map = textures.get("_BaseMap") or textures.get("_MainTex") or {}
        texture_ref = base_map.get("m_Texture")
        tex_file, tex_guid = uy.ref(texture_ref)
        tex_rel = self.db.path(tex_guid) if tex_guid else None
        if tex_rel:
            if tex_rel.lower().endswith(".rendertexture"):
                out.append(("render_texture", self.asset_ref(texture_ref)))
            else:
                self.request(tex_rel)
                out.append(("texture", self.res(tex_rel)))
        scale = base_map.get("m_Scale") or {}
        offset = base_map.get("m_Offset") or {}
        if scale and (float(scale.get("x", 1)) != 1 or float(scale.get("y", 1)) != 1):
            out.append(("tiling", mt.Vec2((float(scale.get("x", 1)), float(scale.get("y", 1))))))
        if offset and (float(offset.get("x", 0)) or float(offset.get("y", 0))):
            out.append(("offset", mt.Vec2((float(offset.get("x", 0)), float(offset.get("y", 0))))))
        smoothness = floats.get("_Smoothness", floats.get("_Glossiness", 0.5))
        out.append(("roughness", 1.0 - float(smoothness)))
        out.append(("metallic", float(floats.get("_Metallic", 0))))
        if "_EMISSION" in keywords and colors.get("_EmissionColor"):
            e = colors["_EmissionColor"]
            out.append(("emission", mt.Color((float(e.get("r", 0)), float(e.get("g", 0)),
                                              float(e.get("b", 0)), 1.0))))
        if float(floats.get("_Surface", 0)) >= 0.5:
            out.append(("surface", "transparent"))
        cull = floats.get("_Cull")
        if cull is not None and int(cull) != 2:
            out.append(("cull", {0: "off", 1: "front"}.get(int(cull), "back")))
        if "Unlit" in shader_rel or "Unlit" in str(mat.get("m_Name", "")):
            out.append(("unlit", True))
        self.write(self.out_rel(rel), mt.asset("Material", out))

    def convert_physic_material(self, rel):
        objects = uy.parse_file(os.path.join(self.unity, rel))
        data = next(iter(objects.values())).data if objects else {}
        friction = float(data.get("dynamicFriction", 0.6))
        bounce = float(data.get("bounciness", 0))
        self.write(self.out_rel(rel), mt.asset("PhysicsMaterial",
                                               [("friction", friction), ("restitution", bounce)]))

    def convert_render_texture(self, rel):
        objects = uy.parse_file(os.path.join(self.unity, rel))
        data = next(iter(objects.values())).data if objects else {}
        self.write(self.out_rel(rel), mt.asset("RenderTexture", [
            ("width", int(data.get("m_Width", 1024))),
            ("height", int(data.get("m_Height", 1024)))]))

    def convert_asset(self, rel):
        objects = uy.parse_file(os.path.join(self.unity, rel))
        main = None
        for o in objects.values():
            if o.file_id == 11400000 or main is None:
                main = o
        if main is None or main.cls != uy.MONO_BEHAVIOUR:
            return
        name = self.script_name(rs.RComp(main.file_id, main.cls, main.type_name, main.data))
        if name == "PanelSettings":
            self.convert_panel_settings(rel, main.data)
            return
        if name == "VolumeProfile" or "components" in main.data:
            self.convert_volume_profile(rel, objects)
            return
        table = getattr(self.script_map, "DATA_ASSETS", {}) if self.script_map else {}
        entry = table.get(name)
        if entry is None:
            self.warn(f"{rel}: data asset {name} is not converted")
            return
        type_name, fn = entry
        self.write(self.out_rel(rel), mt.asset(type_name, fn(main.data, self)))

    def convert_panel_settings(self, rel, d):
        res = d.get("m_ReferenceResolution") or {}
        scale_mode = {0: "ConstantPixelSize", 1: "ConstantPhysicalSize",
                      2: "ScaleWithScreenSize"}.get(int(d.get("m_ScaleMode", 0) or 0),
                                                    "ConstantPixelSize")
        props = [
            ("scale_mode", scale_mode),
            ("reference_resolution", mt.Vec2((float(res.get("x", 1200)), float(res.get("y", 800))))),
            ("match", float(d.get("m_Match", 0) or 0)),
            ("scale", float(d.get("m_Scale", 1) or 1)),
        ]
        # Unity's default runtime theme is the engine's default too.
        self.write(self.out_rel(rel), mt.asset("PanelSettings", props))

    def convert_volume_profile(self, rel, objects):
        def value(component, field, default):
            item = component.get(field) or {}
            if not int(item.get("m_OverrideState", 0) or 0):
                return default
            return item.get("m_Value", default)
        props = []
        for o in objects.values():
            name = self.script_name(rs.RComp(o.file_id, o.cls, o.type_name, o.data))
            if name not in ("Tonemapping", "Bloom", "Vignette", "ColorAdjustments"):
                name = o.data.get("m_Name")
            d = o.data
            if not int(d.get("active", 1) or 0):
                continue
            if name == "Tonemapping":
                mode = int(value(d, "mode", 0) or 0)
                props.append(("tonemap", {0: "none", 1: "neutral", 2: "aces"}.get(mode, "none")))
            elif name == "Bloom":
                props.append(("bloom_intensity", float(value(d, "intensity", 0) or 0)))
                props.append(("bloom_threshold", float(value(d, "threshold", 0.9) or 0.9)))
                props.append(("bloom_scatter", float(value(d, "scatter", 0.7) or 0.7)))
            elif name == "Vignette":
                props.append(("vignette_intensity", float(value(d, "intensity", 0) or 0)))
                props.append(("vignette_smoothness", float(value(d, "smoothness", 0.2) or 0.2)))
                c = value(d, "center", {"x": 0.5, "y": 0.5}) or {}
                props.append(("vignette_center", mt.Vec2((float(c.get("x", 0.5)),
                                                          float(c.get("y", 0.5))))))
            elif name == "ColorAdjustments":
                exposure = float(value(d, "postExposure", 0) or 0)
                if exposure:
                    props.append(("exposure", 2.0 ** exposure))
        self.write(self.out_rel(rel), mt.asset("PostProcessProfile", props))

    def convert_prefab(self, rel):
        guid = self.db.guid_by_path.get(rel)
        roots = self.resolver.resolve_guid(guid) if guid else []
        if not roots:
            self.warn(f"{rel}: empty prefab")
            return
        converter = cv.Converter(self, roots[:1])
        self.write(self.out_rel(rel), cv.write_tree(converter.root, "mprefab"))

    def convert_model_prefab(self, rel):
        guid = self.db.guid_by_path.get(rel)
        if guid is None:
            return
        self.copy(rel)
        roots = self.resolver.resolve_guid(guid)
        if not roots:
            return
        converter = cv.Converter(self, roots[:1])
        self.write(self.model_prefab_rel(rel), cv.write_tree(converter.root, "mprefab"))

    # ---- animator controllers

    def convert_controller(self, rel):
        objects = uy.parse_file(os.path.join(self.unity, rel))
        controller = next((o.data for o in objects.values() if o.cls == 91), None)
        if controller is None:
            return
        sections = [mt.section("manimator", [("format", 1)])]
        model_rel = None
        clip_names = {}

        def clip_name(motion):
            nonlocal model_rel
            file_id, guid = uy.ref(motion)
            if not guid:
                return None
            path = self.db.path(guid)
            if path is None:
                return None
            if path.lower().endswith(".fbx"):
                model_rel = model_rel or path
                if path not in clip_names:
                    clip_names[path] = self.model_clip_ids(path)
                return clip_names[path].get(file_id)
            return None

        types = {1: "float", 3: "int", 4: "bool", 9: "trigger"}
        for p in controller.get("m_AnimatorParameters") or []:
            t = types.get(int(p.get("m_Type", 1)), "float")
            default = {"float": float(p.get("m_DefaultFloat", 0) or 0),
                       "int": int(p.get("m_DefaultInt", 0) or 0),
                       "bool": bool(int(p.get("m_DefaultBool", 0) or 0)),
                       "trigger": False}[t]
            sections.append(mt.section("parameter", [("name", p.get("m_Name")), ("type", t),
                                                     ("default", default)]))

        state_names = {}
        for layer in controller.get("m_AnimatorLayers") or []:
            machine_id, _ = uy.ref(layer.get("m_StateMachine"))
            machine = objects.get(machine_id)
            if machine is None:
                continue
            layer_name = layer.get("m_Name") or "Layer"
            mask_rel = self.db.path(uy.ref(layer.get("m_Mask"))[1] or "") or ""
            sections.append(mt.section("layer", [
                ("name", layer_name),
                ("weight", float(layer.get("m_DefaultWeight", 1) or 0)),
                ("blending", "additive" if int(layer.get("m_BlendingMode", 0) or 0) else "override"),
            ]))
            default_id, _ = uy.ref(machine.data.get("m_DefaultState"))
            child_states = [uy.ref(s.get("m_State"))[0]
                            for s in machine.data.get("m_ChildStates") or []]
            if not default_id and child_states:
                default_id = child_states[0]
            for sid in child_states:
                state = objects.get(sid)
                if state is None:
                    continue
                d = state.data
                state_names[sid] = d.get("m_Name")
                motion_id, motion_guid = uy.ref(d.get("m_Motion"))
                attrs = [("layer", layer_name), ("name", d.get("m_Name")),
                         ("speed", float(d.get("m_Speed", 1) or 1))]
                if sid == default_id:
                    attrs.append(("default", True))
                body = []
                if int(d.get("m_SpeedParameterActive", 0) or 0) and d.get("m_SpeedParameter"):
                    body.append(("speed_parameter", d.get("m_SpeedParameter")))
                if motion_guid:
                    name = clip_name(d.get("m_Motion"))
                    if name:
                        body.append(("clip", name))
                elif motion_id in objects:
                    tree = objects[motion_id].data
                    blend_type = int(tree.get("m_BlendType", 0) or 0)
                    body.append(("blend", {0: "1d", 1: "2d_simple", 2: "2d_freeform",
                                           3: "2d_cartesian", 4: "direct"}.get(blend_type, "1d")))
                    body.append(("parameter", tree.get("m_BlendParameter") or ""))
                    if blend_type in (1, 2, 3):
                        body.append(("parameter_y", tree.get("m_BlendParameterY") or ""))
                    children = []
                    for child in tree.get("m_Childs") or []:
                        name = clip_name(child.get("m_Motion")) or ""
                        pos = child.get("m_Position") or {}
                        children.append([name, float(child.get("m_Threshold", 0) or 0),
                                         float(pos.get("x", 0) or 0), float(pos.get("y", 0) or 0),
                                         float(child.get("m_TimeScale", 1) or 1)])
                    body.append(("motions", children))
                sections.append(mt.section("state", attrs, body))
            # Transitions of this layer's states (and Any State).
            def emit_transition(tid, from_name):
                t = objects.get(tid)
                if t is None:
                    return
                d = t.data
                dst, _ = uy.ref(d.get("m_DstState"))
                if dst not in state_names and dst in objects:
                    state_names[dst] = objects[dst].data.get("m_Name")
                if dst not in state_names:
                    return
                if int(d.get("m_Mute", 0) or 0):
                    return
                conditions = []
                modes = {1: "if", 2: "ifnot", 3: "greater", 4: "less", 6: "equals",
                         7: "notequal"}
                for c in d.get("m_Conditions") or []:
                    conditions.append([c.get("m_ConditionEvent") or "",
                                       modes.get(int(c.get("m_ConditionMode", 1)), "if"),
                                       float(c.get("m_EventTreshold", 0) or 0)])
                attrs = [("layer", layer_name), ("from", from_name), ("to", state_names[dst])]
                body = [
                    ("duration", float(d.get("m_TransitionDuration", 0.25) or 0)),
                    ("fixed_duration", bool(int(d.get("m_HasFixedDuration", 1) or 0))),
                    ("offset", float(d.get("m_TransitionOffset", 0) or 0)),
                    ("has_exit_time", bool(int(d.get("m_HasExitTime", 0) or 0))),
                    ("exit_time", float(d.get("m_ExitTime", 1) or 0)),
                    ("conditions", conditions),
                ]
                sections.append(mt.section("transition", attrs, body))
            for sid in child_states:
                state = objects.get(sid)
                if state is None:
                    continue
                for entry in state.data.get("m_Transitions") or []:
                    emit_transition(uy.ref(entry)[0], state.data.get("m_Name"))
            for entry in machine.data.get("m_AnyStateTransitions") or []:
                emit_transition(uy.ref(entry)[0], "Any State")
        if model_rel:
            self.request(model_rel)
            loops, root_node = self.model_clip_settings(model_rel)
            body = [("model", self.res(model_rel)), ("loop_clips", loops)]
            if root_node:
                body.append(("root_motion_node", root_node))
            sections.insert(1, mt.section("model", [], body))
        self.write(self.out_rel(rel), mt.document(sections))

    def model_clip_settings(self, rel):
        """(names of looping clips, root motion node) from a model's meta."""
        meta = os.path.join(self.unity, rel + ".meta")
        loops, root = [], ""
        if not os.path.exists(meta):
            return loops, root
        with open(meta, encoding="utf-8", errors="replace") as f:
            data = yaml.load(f, Loader=uy._Loader) or {}
        importer = data.get("ModelImporter") or {}
        animations = importer.get("animations") or {}
        root = (animations.get("motionNodeName") or "").split("/")[-1]
        for clip in animations.get("clipAnimations") or []:
            if int(clip.get("loopTime", 0) or 0):
                loops.append(clip.get("takeName") or clip.get("name"))
        return loops, root

    def model_clip_ids(self, rel):
        """{file id: clip name} of a model's animation clips, from its meta."""
        result = {}
        meta = os.path.join(self.unity, rel + ".meta")
        if not os.path.exists(meta):
            return result
        with open(meta, encoding="utf-8", errors="replace") as f:
            data = yaml.load(f, Loader=uy._Loader) or {}
        importer = data.get("ModelImporter") or {}
        for clip in (importer.get("animations") or {}).get("clipAnimations") or []:
            if "internalID" in clip:
                result[int(clip["internalID"])] = clip.get("takeName") or clip.get("name")
        for entry in importer.get("internalIDToNameTable") or []:
            first = entry.get("first") or {}
            for k, v in first.items():
                if int(k) == 74:  # AnimationClip
                    result[int(v)] = entry.get("second")
        return result

    # ---- input actions

    def convert_input_actions(self, rel):
        with open(os.path.join(self.unity, rel), encoding="utf-8") as f:
            data = json.load(f)
        sections = [mt.section("minput", [("format", 1)])]
        for action_map in data.get("maps", []):
            map_name = action_map.get("name", "")
            actions = {a["id"]: a for a in action_map.get("actions", [])}
            bindings = {a["id"]: [] for a in action_map.get("actions", [])}
            composites = {a["id"]: [] for a in action_map.get("actions", [])}
            current = None
            for b in action_map.get("bindings", []):
                action = next((a for a in actions.values() if a["name"] == b.get("action")), None)
                if action is None:
                    continue
                if b.get("isComposite"):
                    current = {"action": action["id"], "parts": {"up": [], "down": [],
                                                                 "left": [], "right": []}}
                    composites[action["id"]].append(current)
                elif b.get("isPartOfComposite") and current is not None:
                    part = b.get("name", "").lower()
                    if part in current["parts"]:
                        current["parts"][part].append(b.get("path", ""))
                else:
                    current = None
                    if b.get("path"):
                        bindings[action["id"]].append(b.get("path"))
            for action_id, action in actions.items():
                kind = action.get("type", "Button").lower()
                attrs = [("name", f"{map_name}/{action['name']}"), ("type", kind)]
                if action.get("expectedControlType") == "Vector2":
                    attrs.append(("control", "vector2"))
                body = [("bindings", bindings[action_id])]
                if composites[action_id]:
                    body.append(("composites", [
                        ["2DVector", c["parts"]["up"], c["parts"]["down"], c["parts"]["left"],
                         c["parts"]["right"]] for c in composites[action_id]]))
                sections.append(mt.section("action", attrs, body))
        self.write(self.out_rel(rel), mt.document(sections))
        return "res://" + self.out_rel(rel)

    # ---- scenes

    def scene_environment(self, objects):
        env = {}
        settings = next((o.data for o in objects.values() if o.cls == uy.RENDER_SETTINGS), None)
        if settings:
            mode = int(settings.get("m_AmbientMode", 0) or 0)
            intensity = float(settings.get("m_AmbientIntensity", 1) or 1)
            if mode == 0:
                # Ambient from Unity's default sky (in linear units).
                env["ambient_sky"] = tuple(c * intensity for c in (0.42, 0.47, 0.55))
                env["ambient_ground"] = tuple(c * intensity for c in (0.20, 0.19, 0.18))
            else:
                sky = cv.color(settings.get("m_AmbientSkyColor"))
                ground = cv.color(settings.get("m_AmbientGroundColor"))
                if mode == 3:  # flat
                    ground = sky
                env["ambient_sky"] = tuple(cv.srgb_to_linear(sky[i]) for i in range(3))
                env["ambient_ground"] = tuple(cv.srgb_to_linear(ground[i]) for i in range(3))
        return env

    def convert_scene(self, rel, skip_prefabs=()):
        path = os.path.join(self.unity, rel)
        objects = self.resolver.parse(path)
        roots = self.resolver.resolve_file(path)
        roots = [r for r in roots if r.instance_guid not in skip_prefabs]
        name = re.sub(r"\W", "", os.path.splitext(os.path.basename(rel))[0])
        converter = cv.Converter(self, roots, root_name=name,
                                 environment=self.scene_environment(objects))
        map_rel = f"maps/{name}.mmap"
        self.write(map_rel, cv.write_tree(converter.root, "mmap"))
        return map_rel


def read_project_settings(ctx):
    """Layers, tags, gravity and tick rate from ProjectSettings."""
    settings = {}
    pdir = os.path.join(ctx.unity, "ProjectSettings")

    def load(name):
        path = os.path.join(pdir, name)
        if not os.path.exists(path):
            return {}
        objects = uy.parse_file(path)
        return next(iter(objects.values())).data if objects else {}

    tags = load("TagManager.asset")
    settings["layers"] = [str(x or "") for x in tags.get("layers", [])][:32]
    settings["tags"] = [str(x) for x in tags.get("tags", []) if x]
    dynamics = load("DynamicsManager.asset")
    g = dynamics.get("m_Gravity") or {"x": 0, "y": -9.81, "z": 0}
    settings["gravity"] = cv.unity_position(g)
    settings["queries_hit_triggers"] = bool(int(dynamics.get("m_QueriesHitTriggers", 1) or 0))
    time_manager = load("TimeManager.asset")
    step = float(time_manager.get("Fixed Timestep", 0.02) or 0.02)
    if isinstance(time_manager.get("Fixed Timestep"), dict):
        fixed = time_manager["Fixed Timestep"]
        step = float(fixed.get("m_Count", 2822399)) / float(fixed.get("m_Rate", {}).get("m_Numerator", 141120000) or 1) \
            if "m_Count" in fixed else 0.02
    settings["tick_rate"] = max(1, round(1.0 / step))
    build = load("EditorBuildSettings.asset")
    settings["scenes"] = [s.get("path") for s in build.get("m_Scenes", []) if int(s.get("enabled", 1))]
    player = load("ProjectSettings.asset")
    settings["product"] = player.get("productName") or "Game"
    settings["version"] = str(player.get("bundleVersion") or "dev")
    settings["width"] = int(player.get("defaultScreenWidth", 1600) or 1600)
    settings["height"] = int(player.get("defaultScreenHeight", 900) or 900)
    return settings


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--unity", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--scripts")
    parser.add_argument("--model-info")
    parser.add_argument("--substitute", action="append")
    parser.add_argument("--name")
    parser.add_argument("--skip-prefab", action="append", default=[],
                        help="Unity path of a prefab whose scene instances are left out")
    args = parser.parse_args()

    ctx = Context(args)
    if ctx.script_map and hasattr(ctx.script_map, "setup"):
        ctx.script_map.setup(ctx)
    settings = read_project_settings(ctx)
    skip = {ctx.db.guid_by_path.get(p) for p in args.skip_prefab}

    ctx.copy_all()
    maps = []
    scene_dir = os.path.join(ctx.unity, "Assets", "Scenes")
    for dirpath, _dirs, files in os.walk(scene_dir):
        for name in sorted(files):
            if name.endswith(".unity"):
                rel = os.path.relpath(os.path.join(dirpath, name), ctx.unity).replace(os.sep, "/")
                print("scene", rel)
                maps.append((rel, ctx.convert_scene(rel, skip)))
                ctx.run()
    ctx.run()

    actions = None
    for guid, rel in ctx.db.by_guid.items():
        if rel.startswith("Assets/") and rel.endswith(".inputactions"):
            actions = ctx.convert_input_actions(rel)

    main_map = maps[0][1] if maps else ""
    for scene in settings.get("scenes", []):
        for rel, out in maps:
            if rel == scene:
                main_map = out
                break
        else:
            continue
        break

    name = args.name or settings["product"]
    project = [
        mt.section("project", [], [("name", name), ("main_map", "res://" + main_map),
                                   ("version", settings["version"]),
                                   ("engine_version", "dev")]),
        mt.section("window", [], [("title", name), ("width", settings["width"]),
                                  ("height", settings["height"])]),
        mt.section("physics", [], [("gravity", mt.Vec3(settings["gravity"])),
                                   ("tick_rate", settings["tick_rate"]),
                                   ("queries_hit_triggers", settings["queries_hit_triggers"])]),
        mt.section("layers", [], [("names", settings["layers"])]),
        mt.section("tags", [], [("names", settings["tags"])]),
        mt.section("import", [], [("model_convention", "unity")]),
    ]
    if actions:
        project.append(mt.section("input", [], [("actions", actions)]))
    render = getattr(ctx.script_map, "RENDER_SETTINGS", None) if ctx.script_map else None
    if render:
        project.append(mt.section("render", [], render(ctx)))
        ctx.run()  # the assets the render settings name
    file_name = re.sub(r"\W", "_", name.lower()) + ".mproj"
    existing = os.path.join(ctx.out, file_name)
    ctx.write(file_name, mt.document(project))
    print(f"wrote {existing} with {len(maps)} maps; {len(ctx.warnings)} warnings")


if __name__ == "__main__":
    main()
