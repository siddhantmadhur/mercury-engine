"""Converts resolved Unity objects (resolve.RNode trees) into Mercury nodes.

Unity is left-handed with +Z forward; Mercury is right-handed with -Z
forward. Mirroring Z maps one onto the other:

    position (x, y, z)        -> (x, y, -z)
    rotation (x, y, z, w)     -> (-x, -y, z, w)

Models are loaded by the engine with the "unity" convention, which applies
the matching change to their geometry (see mercury/properties/model.h).
"""

import math
import os
import re

import mtext as mt
import resolve as rs
import unity_yaml as uy

BUILTIN_MESHES = {10202: "cube", 10209: "plane", 10208: "capsule", 10207: "sphere",
                  10206: "cylinder", 10210: "quad"}
# Unity's built-in shapes measured in Mercury's (see primitives.h):
# the plane is 10 x 10 units.
BUILTIN_MESH_SCALE = {"plane": (10.0, 1.0, 10.0)}

# Scripts Unity and its packages provide, by class name.
URP_CAMERA_DATA = "UniversalAdditionalCameraData"
IGNORED_SCRIPTS = {"UniversalAdditionalLightData", "UniversalAdditionalCameraData",
                   "DebugLogManager", "DebugLogPopup", "DebugLogRecycledListView",
                   "DebugLogItem", "DebugsOnScrollListener", "DebugLogResizeListener",
                   "EventSystem", "InputSystemUIInputModule", "StandaloneInputModule"}


# --------------------------------------------------------------- math ---

def unity_position(v):
    return (float(v.get("x", 0)), float(v.get("y", 0)), -float(v.get("z", 0)))


def unity_rotation(q):
    return (-float(q.get("x", 0)), -float(q.get("y", 0)), float(q.get("z", 0)),
            float(q.get("w", 1)))


def quat_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def axis_angle(axis, degrees):
    half = math.radians(degrees) / 2
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def unity_euler_to_quat(e):
    """Unity's Quaternion.Euler: Z, then X, then Y."""
    qx = axis_angle((1, 0, 0), e[0])
    qy = axis_angle((0, 1, 0), e[1])
    qz = axis_angle((0, 0, 1), e[2])
    return quat_multiply(qy, quat_multiply(qx, qz))


def quat_to_euler(q):
    """glm::eulerAngles (Mercury's Transform::QuatToEuler), in degrees."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    py = 2 * (y * z + w * x)
    px = w * w - x * x - y * y + z * z
    pitch = 2 * math.atan2(x, w) if abs(px) < 1e-7 and abs(py) < 1e-7 else math.atan2(py, px)
    yaw = math.asin(max(-1.0, min(1.0, -2 * (x * z - w * y))))
    ry = 2 * (x * y + w * z)
    rx = w * w + x * x - y * y - z * z
    roll = 0.0 if abs(rx) < 1e-7 and abs(ry) < 1e-7 else math.atan2(ry, rx)
    out = []
    for a in (pitch, yaw, roll):
        d = math.degrees(a)
        out.append(0.0 if abs(d) < 1e-4 else d)
    return tuple(out)


def unity_euler_to_mercury(e):
    """A Unity local euler rotation (degrees) as Mercury euler degrees."""
    q = unity_euler_to_quat(e)
    return quat_to_euler((-q[0], -q[1], q[2], q[3]))


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def color(c, default=(1, 1, 1, 1)):
    if not isinstance(c, dict):
        return mt.Color(default)
    return mt.Color((float(c.get("r", default[0])), float(c.get("g", default[1])),
                     float(c.get("b", default[2])), float(c.get("a", default[3]))))


def vec3(v, default=(0, 0, 0)):
    if not isinstance(v, dict):
        return mt.Vec3(default)
    return mt.Vec3((float(v.get("x", default[0])), float(v.get("y", default[1])),
                    float(v.get("z", default[2]))))


def mask_bits(v):
    """A LayerMask as a signed 32-bit int (-1 = everything)."""
    bits = v.get("m_Bits", 0) if isinstance(v, dict) else (v or 0)
    bits = int(bits) & 0xFFFFFFFF
    return bits - (1 << 32) if bits >= (1 << 31) else bits


# ------------------------------------------------------------ the tree ---

class MNode:
    def __init__(self, name):
        self.name = name
        self.tag = ""
        self.layer = 0
        self.active = True
        self.components = []  # [(type, [(key, value), ...])]
        self.children = []
        self.parent = None
        self.source = None    # the RNode

    def path(self):
        if self.parent is None:
            return "."
        if self.parent.parent is None:
            return self.name
        return self.parent.path() + "/" + self.name

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()

    def component(self, type_name):
        for t, props in self.components:
            if t == type_name:
                return props
        return None


def sanitize_name(name):
    name = re.sub(r'[/"\\\n\r]', "", str(name)).strip()
    return name if name and name not in (".", "..") else "Node"


class Converter:
    """Converts one scene or prefab; `ctx` knows the whole project."""

    def __init__(self, ctx, roots, root_name=None, environment=None):
        self.ctx = ctx
        self.environment = environment or {}
        self.by_key = {}  # unity object key -> MNode
        if root_name is not None:  # a map: an extra root holds the scene roots
            self.root = MNode(root_name)
            for r in roots:
                self._build(r, self.root)
        else:
            if len(roots) != 1:
                ctx.warn(f"prefab with {len(roots)} roots; keeping the first")
            self.root = self._build(roots[0], None)
        self._camera_stacks()
        for node in self.root.walk():
            if node.source is not None:
                self._convert(node)

    # ---- structure

    def _build(self, rnode, parent):
        node = MNode(sanitize_name(rnode.name))
        node.source = rnode
        node.tag = "" if rnode.tag in (None, "", "Untagged") else rnode.tag
        node.layer = rnode.layer
        node.active = rnode.active
        if parent is not None:
            taken = {c.name for c in parent.children}
            base, i = node.name, 1
            while node.name in taken:
                node.name = f"{base} ({i})"
                i += 1
            node.parent = parent
            parent.children.append(node)
        for key in (rnode.key, rnode.go_key):
            if key is not None:
                self.by_key[key] = node
        if rnode.transform is not None:
            self.by_key[rnode.transform.key] = node
        for comp in rnode.components:
            self.by_key[comp.key] = node
        for child in rnode.children:
            self._build(child, node)
        return node

    def node_path(self, reference):
        file_id, guid = uy.ref(reference)
        if not file_id or guid:
            return None
        node = self.by_key.get(file_id)
        return mt.NodePath(node.path()) if node is not None else None

    def component_of(self, reference):
        """(MNode, RComp) a same-file reference points at."""
        file_id, _ = uy.ref(reference)
        node = self.by_key.get(file_id)
        if node is None or node.source is None:
            return None, None
        for comp in node.source.components:
            if comp.key == file_id:
                return node, comp
        return node, None

    def _camera_stacks(self):
        # Overlay cameras listed in a base camera's URP stack.
        self.overlay_base = {}
        for node in self.root.walk():
            if node.source is None:
                continue
            for comp in node.source.components:
                if comp.cls != uy.MONO_BEHAVIOUR:
                    continue
                if self.ctx.script_name(comp) != URP_CAMERA_DATA:
                    continue
                for entry in comp.data.get("m_Cameras") or []:
                    other, _ = self.component_of(entry)
                    if other is not None:
                        self.overlay_base[id(other)] = node.path()

    # ---- components

    def _convert(self, node):
        r = node.source
        comps = node.components
        t = r.transform
        if t is not None:
            d = t.data
            if d.get("_mercury"):
                pos = unity_position_raw(d.get("m_LocalPosition", {}))
                q = d.get("m_LocalRotation", {})
                rot = (float(q.get("x", 0)), float(q.get("y", 0)), float(q.get("z", 0)),
                       float(q.get("w", 1)))
            else:
                pos = unity_position(d.get("m_LocalPosition", {}))
                rot = unity_rotation(d.get("m_LocalRotation", {"w": 1}))
            scale = vec3(d.get("m_LocalScale"), (1, 1, 1))
            props = []
            if any(abs(x) > 1e-7 for x in pos):
                props.append(("position", mt.Vec3(pos)))
            euler = quat_to_euler(rot)
            if any(abs(x) > 1e-5 for x in euler):
                props.append(("rotation", mt.Vec3(euler)))
            if any(abs(x - 1) > 1e-7 for x in scale):
                props.append(("scale", scale))
            comps.append(("Transform", props))

        by_cls = {}
        for c in r.components:
            by_cls.setdefault(c.cls, []).append(c)
        has_text = any(self.ctx.script_name(c) in ("TextMeshPro", "TextMeshProUGUI")
                       for c in by_cls.get(uy.MONO_BEHAVIOUR, []))

        for c in r.components:
            enabled = int(c.data.get("m_Enabled", 1) or 0) != 0
            if c.cls == uy.MESH_RENDERER and not has_text:
                self._mesh_renderer(node, c, by_cls)
            elif c.cls == uy.BOX_COLLIDER and enabled:
                props = [("size", vec3(c.data.get("m_Size"), (1, 1, 1)))]
                props += self._collider_common(c)
                comps.append(("BoxCollider", props))
            elif c.cls == uy.SPHERE_COLLIDER and enabled:
                props = [("radius", float(c.data.get("m_Radius", 0.5)))]
                props += self._collider_common(c)
                comps.append(("SphereCollider", props))
            elif c.cls == uy.CAPSULE_COLLIDER and enabled:
                props = [("radius", float(c.data.get("m_Radius", 0.5))),
                         ("height", float(c.data.get("m_Height", 2)))]
                props += self._collider_common(c)
                comps.append(("CapsuleCollider", props))
            elif c.cls == uy.MESH_COLLIDER and enabled:
                self._mesh_collider(node, c)
            elif c.cls == uy.RIGIDBODY:
                comps.append(("Rigidbody", self._rigidbody(c)))
            elif c.cls == uy.CHARACTER_CONTROLLER and enabled:
                d = c.data
                comps.append(("CharacterController", [
                    ("height", float(d.get("m_Height", 2))),
                    ("radius", float(d.get("m_Radius", 0.5))),
                    ("center", mt.Vec3(unity_position(d.get("m_Center", {})))),
                    ("slope_limit", float(d.get("m_SlopeLimit", 45))),
                    ("step_offset", float(d.get("m_StepOffset", 0.3))),
                    ("skin_width", float(d.get("m_SkinWidth", 0.08))),
                    ("min_move_distance", float(d.get("m_MinMoveDistance", 0.001))),
                ]))
            elif c.cls == uy.CAMERA:
                self._camera(node, c, by_cls)
            elif c.cls == uy.LIGHT:
                self._light(node, c)
            elif c.cls == uy.AUDIO_SOURCE:
                self._audio_source(node, c)
            elif c.cls == uy.AUDIO_LISTENER:
                comps.append(("AudioListener", [("enabled", enabled)]))
            elif c.cls == uy.ANIMATOR:
                self._animator(node, c)
            elif c.type_name == "PanelRenderer" or self.ctx.script_name(c) == "UIDocument":
                self._ui_document(node, c)
            elif c.cls == uy.MONO_BEHAVIOUR:
                self._mono_behaviour(node, c)

    def _collider_common(self, c):
        props = []
        center = unity_position(c.data.get("m_Center", {}))
        if any(abs(x) > 1e-7 for x in center):
            props.append(("center", mt.Vec3(center)))
        material = self.ctx.asset_ref(c.data.get("m_Material"))
        if material:
            props.append(("material", material))
        if int(c.data.get("m_IsTrigger", 0) or 0):
            props.append(("is_trigger", True))
        return props

    def _mesh_source(self, mesh_ref, hint_node=None):
        """(shape, model res, node name, mesh_scale) of a mesh reference."""
        file_id, guid = uy.ref(mesh_ref)
        if guid == rs.BUILTIN_GUID or (guid is None and file_id in BUILTIN_MESHES):
            shape = BUILTIN_MESHES.get(file_id)
            if shape is None:
                self.ctx.warn(f"unknown built-in mesh {file_id}")
                return None
            return shape, None, "", BUILTIN_MESH_SCALE.get(shape)
        if not guid:
            return None
        guid = self.ctx.substitute(guid)
        path = self.ctx.db.path(guid)
        if path is None or not path.lower().endswith((".fbx", ".obj", ".gltf", ".glb")):
            self.ctx.warn(f"mesh from {path or guid} is not supported")
            return None
        node_name = hint_node
        if node_name is None:
            node_name = self.ctx.default_mesh_node(path)
        return "model", self.ctx.res(path), node_name, None

    def _mesh_renderer(self, node, renderer, by_cls):
        filters = by_cls.get(uy.MESH_FILTER, [])
        if not filters:
            return
        mf = filters[0]
        source = self._mesh_source(mf.data.get("m_Mesh"), mf.data.get("_mercury_node"))
        if source is None:
            return
        shape, model, node_name, mesh_scale = source
        d = renderer.data
        props = [("mesh", shape)]
        if model:
            props.append(("model", model))
            if node_name:
                props.append(("node", node_name))
        if mesh_scale:
            props.append(("mesh_scale", mt.Vec3(mesh_scale)))
        materials = []
        for entry in d.get("m_Materials") or []:
            materials.append(self.ctx.material_ref(entry))
        if shape != "model":
            if materials and materials[0]:
                props.append(("material", materials[0]))
        elif any(materials):
            props.append(("materials", [m or "" for m in materials]))
        if not int(d.get("m_Enabled", 1) or 0):
            props.append(("visible", False))
        if not int(d.get("m_CastShadows", 1) or 0):
            props.append(("cast_shadows", False))
        layers = int(d.get("m_RenderingLayerMask", 1) or 0)
        if layers != 1:
            props.append(("rendering_layers", layers))
        node.components.append(("MeshRenderer", props))

    def _mesh_collider(self, node, c):
        d = c.data
        mesh_ref = d.get("m_Mesh")
        hint = None
        if not uy.ref(mesh_ref)[0]:
            # No mesh set: Unity uses the MeshFilter's.
            for comp in node.source.components:
                if comp.cls == uy.MESH_FILTER:
                    mesh_ref = comp.data.get("m_Mesh")
                    hint = comp.data.get("_mercury_node")
        source = self._mesh_source(mesh_ref, hint)
        if source is None:
            return
        shape, model, node_name, mesh_scale = source
        props = [("mesh", shape)]
        if model:
            props.append(("model", model))
            if node_name:
                props.append(("submesh", node_name))
        if mesh_scale:
            props.append(("mesh_scale", mt.Vec3(mesh_scale)))
        if int(d.get("m_Convex", 0) or 0):
            props.append(("convex", True))
        props += [p for p in self._collider_common(c) if p[0] != "center"]
        node.components.append(("MeshCollider", props))

    def _rigidbody(self, c):
        d = c.data
        props = [("mass", float(d.get("m_Mass", 1)))]
        if int(d.get("m_IsKinematic", 0) or 0):
            props.append(("motion_type", "kinematic"))
        if not int(d.get("m_UseGravity", 1) or 0):
            props.append(("use_gravity", False))
        drag = d.get("m_LinearDamping", d.get("m_Drag", 0))
        angular = d.get("m_AngularDamping", d.get("m_AngularDrag", 0.05))
        props.append(("linear_damping", float(drag or 0)))
        props.append(("angular_damping", float(angular or 0)))
        if int(d.get("m_CollisionDetection", 0) or 0):
            props.append(("motion_quality", "linear_cast"))
        constraints = int(d.get("m_Constraints", 0) or 0)
        for bit, key in ((2, "lock_translation_x"), (4, "lock_translation_y"),
                         (8, "lock_translation_z"), (16, "lock_rotation_x"),
                         (32, "lock_rotation_y"), (64, "lock_rotation_z")):
            if constraints & bit:
                props.append((key, True))
        return props

    def _camera(self, node, c, by_cls):
        d = c.data
        urp = None
        for comp in by_cls.get(uy.MONO_BEHAVIOUR, []):
            if self.ctx.script_name(comp) == URP_CAMERA_DATA:
                urp = comp.data
        clear_flags = int(d.get("m_ClearFlags", 1) or 1)
        clear = {1: "skybox", 2: "color", 3: "depth_only", 4: "nothing"}.get(clear_flags, "skybox")
        overlay = bool(urp) and int(urp.get("m_CameraType", 0) or 0) == 1
        props = [
            ("fov", float(d.get("field of view", d.get("m_FieldOfView", 60)))),
            ("near_clip", float(d.get("near clip plane", d.get("m_NearClipPlane", 0.3)))),
            ("far_clip", float(d.get("far clip plane", d.get("m_FarClipPlane", 1000)))),
            ("background", color(d.get("m_BackGroundColor"), (0.19, 0.3, 0.47, 0))),
            ("clear", clear),
            ("current", node.source.tag == "MainCamera" or not overlay),
            ("enabled", bool(int(d.get("m_Enabled", 1) or 0))),
            ("culling_mask", mask_bits(d.get("m_CullingMask", {}))),
            ("depth", float(d.get("m_Depth", 0) or 0)),
        ]
        if overlay:
            props.append(("overlay", True))
            base = self.overlay_base.get(id(node))
            if base:
                props.append(("base_camera", mt.NodePath(base)))
        target = self.ctx.asset_ref(d.get("m_TargetTexture"))
        if target:
            props.append(("target_texture", target))
        if urp is not None and not int(urp.get("m_RenderPostProcessing", 0) or 0):
            props.append(("post_processing", False))
        node.components.append(("Camera", props))

    def _light(self, node, c):
        d = c.data
        light_type = int(d.get("m_Type", 1) or 0)
        if light_type != 1:
            self.ctx.warn(f"{node.path()}: only directional lights are converted")
            return
        col = color(d.get("m_Color"))
        linear = [srgb_to_linear(col[i]) for i in range(3)]
        if int(d.get("m_UseColorTemperature", 0) or 0):
            tint = kelvin_to_linear(float(d.get("m_ColorTemperature", 6570)))
            linear = [linear[i] * tint[i] for i in range(3)]
        env = self.environment
        props = [
            ("color", mt.Color((linear[0], linear[1], linear[2], 1.0))),
            ("intensity", float(d.get("m_Intensity", 1))),
            ("ambient", mt.Color(env.get("ambient_sky", (0.2, 0.2, 0.2)) + (1.0,))),
            ("ground_ambient", mt.Color(env.get("ambient_ground", (0.07, 0.07, 0.07)) + (1.0,))),
            ("cast_shadows", int((d.get("m_Shadows") or {}).get("m_Type", 0) or 0) != 0),
            ("shadow_distance", float(env.get("shadow_distance", 50))),
        ]
        node.components.append(("DirectionalLight", props))

    def _audio_source(self, node, c):
        d = c.data
        spatial = 0.0
        curve = (d.get("panLevelCustomCurve") or {}).get("m_Curve") or []
        if curve:
            spatial = float(curve[0].get("value", 0) or 0)
        props = [("enabled", bool(int(d.get("m_Enabled", 1) or 0)))]
        clip = self.ctx.asset_ref(d.get("m_audioClip"))
        if clip:
            props.append(("clip", clip))
        props += [
            ("volume", float(d.get("m_Volume", 1))),
            ("pitch", float(d.get("m_Pitch", 1))),
            ("loop", bool(int(d.get("Loop", 0) or 0))),
            ("play_on_awake", bool(int(d.get("m_PlayOnAwake", 1) or 0))),
            ("mute", bool(int(d.get("Mute", 0) or 0))),
            ("spatial_blend", spatial),
            ("min_distance", float((d.get("MinDistance") or 1))),
            ("max_distance", float((d.get("MaxDistance") or 500))),
        ]
        node.components.append(("AudioSource", props))

    def _animator(self, node, c):
        # A model's own Animator under an object that already animates it
        # (Unity's model import adds one to every animated model).
        ancestor = node.parent
        while ancestor is not None:
            if ancestor.source is not None and any(
                    comp.cls == uy.ANIMATOR for comp in ancestor.source.components):
                return
            ancestor = ancestor.parent
        d = c.data
        props = [("enabled", bool(int(d.get("m_Enabled", 1) or 0)))]
        controller = self.ctx.asset_ref(d.get("m_Controller"))
        if controller:
            props.append(("controller", controller))
        if int(d.get("m_ApplyRootMotion", 0) or 0):
            props.append(("apply_root_motion", True))
        node.components.append(("Animator", props))

    def _ui_document(self, node, c):
        d = c.data
        props = []
        source = self.ctx.asset_ref(d.get("sourceAsset") or d.get("m_VisualTreeAsset"))
        if source:
            props.append(("source_asset", source))
        panel = self.ctx.asset_ref(d.get("m_PanelSettings"))
        if panel:
            props.append(("panel_settings", panel))
        order = d.get("m_SortingOrder", d.get("m_SortOrder", 0)) or 0
        if order:
            props.append(("sort_order", float(order)))
        node.components.append(("UIDocument", props))

    def _mono_behaviour(self, node, c):
        name = self.ctx.script_name(c)
        if name is None or name in IGNORED_SCRIPTS:
            return
        if name in ("TextMeshPro", "TextMeshProUGUI"):
            self._text(node, c)
            return
        if name == "Volume":
            d = c.data
            props = []
            profile = self.ctx.asset_ref(d.get("sharedProfile"))
            if profile:
                props.append(("profile", profile))
            props.append(("weight", float(d.get("weight", 1))))
            node.components.append(("PostProcessVolume", props))
            return
        converted = self.ctx.convert_script(name, c, self, node)
        if converted is None:
            self.ctx.warn(f"{node.path()}: script {name} is not converted")
            return
        type_name, props = converted
        if not int(c.data.get("m_Enabled", 1) or 0):
            props = list(props) + [("enabled", False)]
        node.components.append((type_name, props))

    def _text(self, node, c):
        d = c.data
        size = None
        t = node.source.transform
        if t is not None and t.cls == uy.RECT_TRANSFORM:
            size = t.data.get("m_SizeDelta")
        horizontal = int(d.get("m_HorizontalAlignment", 1) or 1)
        vertical = int(d.get("m_VerticalAlignment", 256) or 256)
        h = {1: "left", 2: "center", 4: "right", 8: "center", 16: "center", 32: "center"}.get(
            horizontal, "left")
        v = {256: "top", 512: "middle", 1024: "bottom", 2048: "middle", 4096: "top",
             8192: "middle"}.get(vertical, "top")
        col = d.get("m_fontColor")
        props = [
            ("text", str(d.get("m_text") or "")),
            ("font_size", float(d.get("m_fontSize", 36))),
            ("color", color(col)),
            ("alignment", f"{v}_{h}"),
        ]
        if isinstance(size, dict):
            props.append(("size", mt.Vec2((float(size.get("x", 20)), float(size.get("y", 5))))))
        if not int(d.get("m_enableWordWrapping", d.get("m_TextWrappingMode", 1)) or 0):
            props.append(("word_wrap", False))
        font = self.ctx.text_font(d.get("m_fontAsset"))
        if font:
            props.append(("font", font))
        node.components.append(("TextMesh", props))

    # ---- helpers for script converters

    def event(self, data):
        """A UnityEvent's persistent calls as Mercury's Event array."""
        calls = []
        persistent = ((data or {}).get("m_PersistentCalls") or {}).get("m_Calls") or []
        for call in persistent:
            if int(call.get("m_CallState", 2) or 0) == 0:
                continue
            target_node, target_comp = self.component_of(call.get("m_Target"))
            if target_node is None:
                continue
            type_name = (call.get("m_TargetAssemblyTypeName") or "").split(",")[0].split(".")[-1]
            component = "GameObject"
            if target_comp is not None:
                component = self.ctx.script_type(target_comp) or type_name
            elif type_name and type_name != "GameObject":
                component = self.ctx.script_type_by_class(type_name) or type_name
            method = call.get("m_MethodName") or ""
            mode = int(call.get("m_Mode", 1) or 1)
            args = call.get("m_Arguments") or {}
            item = [mt.NodePath(target_node.path()), component, method]
            if mode == 3:
                item.append(int(args.get("m_IntArgument", 0) or 0))
            elif mode == 4:
                item.append(float(args.get("m_FloatArgument", 0) or 0))
            elif mode == 5:
                item.append(str(args.get("m_StringArgument") or ""))
            elif mode == 6:
                item.append(bool(int(args.get("m_BoolArgument", 0) or 0)))
            calls.append(item)
        return calls


def unity_position_raw(v):
    return (float(v.get("x", 0)), float(v.get("y", 0)), float(v.get("z", 0)))


def kelvin_to_linear(kelvin):
    """Approximate RGB of a black body (Tanner Helland), in linear units,
    normalized so 6570K (Unity's default) is white."""
    def rgb(k):
        t = k / 100.0
        if t <= 66:
            r = 255.0
            g = 99.4708025861 * math.log(t) - 161.1195681661
            b = 0.0 if t <= 19 else 138.5177312231 * math.log(t - 10) - 305.0447927307
        else:
            r = 329.698727446 * (t - 60) ** -0.1332047592
            g = 288.1221695283 * (t - 60) ** -0.0755148492
            b = 255.0
        return [srgb_to_linear(max(0.0, min(255.0, x)) / 255.0) for x in (r, g, b)]
    c = rgb(kelvin)
    w = rgb(6570)
    return [c[i] / w[i] if w[i] > 0 else 1.0 for i in range(3)]


# ------------------------------------------------------------- writing ---

def write_tree(root, header="mmap"):
    """The mtext of a converted tree (a .mmap or .mprefab)."""
    sections = [mt.section(header, [("format", 1)])]
    for node in root.walk():
        attributes = [("name", node.name)]
        if node.parent is not None:
            attributes.append(("parent", node.parent.path()))
        if node.tag:
            attributes.append(("tag", node.tag))
        if node.layer:
            attributes.append(("layer", node.layer))
        if not node.active:
            attributes.append(("active", False))
        sections.append(mt.section("node", attributes))
        path = node.path()
        for type_name, props in node.components:
            sections.append(mt.section("component", [("type", type_name), ("node", path)],
                                       props))
    return mt.document(sections)
