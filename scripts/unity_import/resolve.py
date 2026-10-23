"""Resolves Unity scenes and prefabs into plain object trees.

Prefab instances are expanded (recursively) with their modifications,
added and removed objects applied, so every scene or prefab becomes a tree of
RNodes that hold Unity component data as plain dicts. Each object keeps the
file id it has inside the file being resolved, so later modifications can
find it: an object of a nested instance gets (instance_id ^ source_id) &
0x7FFFFFFFFFFFFFFF, as Unity numbers them.
"""

import copy
import os

import unity_yaml as uy

MASK = 0x7FFFFFFFFFFFFFFF
BUILTIN_GUID = "0000000000000000e000000000000000"

# Model prefabs (an .fbx used as a prefab): the root objects have these ids.
FBX_ROOT_GAME_OBJECT = 919132149155446097
FBX_ROOT_TRANSFORM = -8679921383154817045


def nested_id(instance_id, source_id):
    return (instance_id ^ source_id) & MASK


def remap_refs(value, fn):
    """Rewrites every same-file reference ({fileID: N} without a guid)."""
    if isinstance(value, dict):
        if "fileID" in value and not value.get("guid") and len(value) <= 2:
            file_id = int(value.get("fileID") or 0)
            if file_id:
                value["fileID"] = fn(file_id)
            return
        for v in value.values():
            remap_refs(v, fn)
    elif isinstance(value, list):
        for v in value:
            remap_refs(v, fn)


class RComp:
    __slots__ = ("key", "cls", "type_name", "data", "origin")

    def __init__(self, key, cls, type_name, data, origin=None):
        self.key = key
        self.cls = cls
        self.type_name = type_name
        self.data = data
        # (guid, file id) of the asset object this came from, for debugging
        self.origin = origin

    def script_guid(self):
        script = self.data.get("m_Script")
        return script.get("guid") if isinstance(script, dict) else None


class RNode:
    def __init__(self, key, name):
        self.key = key
        self.go_key = None
        self.name = name
        self.layer = 0
        self.tag = "Untagged"
        self.active = True
        self.transform = None  # RComp of class Transform / RectTransform
        self.components = []   # every other component, in order
        self.children = []
        self.parent = None
        # Set on the root of an expanded prefab instance.
        self.instance_guid = None
        self.instance_key = None
        # Model prefab info: (fbx guid, model node name or "" for the whole)
        self.model_node = None

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()

    def rekey(self, instance_id):
        """Renumbers this subtree as objects of an instance in another file
        (references between them included)."""
        renumber = lambda file_id: nested_id(instance_id, file_id)
        for node in self.walk():
            node.key = renumber(node.key)
            if node.go_key is not None:
                node.go_key = renumber(node.go_key)
            if node.transform:
                node.transform.key = renumber(node.transform.key)
                remap_refs(node.transform.data, renumber)
            for comp in node.components:
                comp.key = renumber(comp.key)
                remap_refs(comp.data, renumber)


class AssetDatabase:
    """Maps guids to asset paths (relative to the Unity project root)."""

    def __init__(self, unity_root):
        self.root = unity_root
        self.by_guid = {}
        for top in ("Assets", "Packages"):
            base = os.path.join(unity_root, top)
            for dirpath, _dirs, files in os.walk(base):
                for name in files:
                    if not name.endswith(".meta"):
                        continue
                    meta = os.path.join(dirpath, name)
                    guid = uy.read_meta_guid(meta)
                    if guid:
                        rel = os.path.relpath(meta[:-5], unity_root)
                        self.by_guid[guid] = rel.replace(os.sep, "/")
        self.guid_by_path = {p: g for g, p in self.by_guid.items()}

    def path(self, guid):
        return self.by_guid.get(guid)

    def abspath(self, guid):
        p = self.path(guid)
        return os.path.join(self.root, p) if p else None


class Resolver:
    def __init__(self, db, model_info):
        self.db = db
        self.model_info = model_info  # callable(abs fbx path) -> info dict
        self._cache = {}
        self.warnings = []
        self._parsed = {}

    def warn(self, message):
        if message not in self.warnings:
            self.warnings.append(message)

    def parse(self, path):
        if path not in self._parsed:
            self._parsed[path] = uy.parse_file(path)
        return self._parsed[path]

    # ----------------------------------------------------------- entry --

    def resolve_guid(self, guid):
        """Roots of the asset with that guid (a prefab or a model)."""
        if guid in self._cache:
            return copy.deepcopy(self._cache[guid])
        path = self.db.abspath(guid)
        roots = []
        if path is None:
            self.warn(f"missing asset {guid}")
        elif path.lower().endswith(".fbx"):
            roots = [self.model_roots(guid, path)]
        elif path.endswith(".prefab"):
            roots = self.resolve_file(path, guid)
        else:
            self.warn(f"cannot instantiate {path}")
        self._cache[guid] = roots
        return copy.deepcopy(roots)

    # ------------------------------------------------------ model files --

    def model_roots(self, guid, path):
        """The objects Unity makes for a model file used as a prefab."""
        info = self.model_info(path) or {}
        nodes = info.get("nodes") or [{"name": os.path.splitext(os.path.basename(path))[0],
                                       "parent": -1, "meshes": []}]
        meshes = info.get("meshes") or []
        skinned = any(m.get("skinned") for m in meshes)
        stem = os.path.splitext(os.path.basename(path))[0]
        materials = self.model_materials(path, info)

        def make(index, key, go_key, transform_key):
            src = nodes[index]
            node = RNode(key, stem if index == 0 else src["name"])
            node.go_key = go_key
            # The root's transform is the instance's own (scenes set it);
            # children keep the model's layout.
            if index == 0:
                pos, rot, scale = [0, 0, 0], [0, 0, 0, 1], [1, 1, 1]
            else:
                pos = list(src.get("position", [0, 0, 0]))
                rot = list(src.get("rotation", [0, 0, 0, 1]))
                scale = list(src.get("scale", [1, 1, 1]))
            node.transform = RComp(transform_key, uy.TRANSFORM, "Transform", {
                "_mercury": True,  # values are already in Mercury space
                "m_LocalPosition": {"x": pos[0], "y": pos[1], "z": pos[2]},
                "m_LocalRotation": {"x": rot[0], "y": rot[1], "z": rot[2], "w": rot[3]},
                "m_LocalScale": {"x": scale[0], "y": scale[1], "z": scale[2]},
            })
            whole = index == 0 and (skinned or len(nodes) == 1)
            own_meshes = src.get("meshes", [])
            if whole or own_meshes:
                draw_node = "" if whole else src["name"]
                # Materials of the meshes drawn, in Unity's submesh order.
                drawn = (range(len(meshes)) if whole else own_meshes)
                slots = sorted({meshes[m]["material"] for m in drawn if m < len(meshes)})
                node.components.append(RComp(key ^ 0x33, uy.MESH_FILTER, "MeshFilter", {
                    "m_Mesh": {"fileID": 4300000, "guid": guid},
                    "_mercury_node": draw_node,
                }))
                node.components.append(RComp(
                    -7511558181221131132 if index == 0 else key ^ 0x23,
                    uy.MESH_RENDERER, "MeshRenderer", {
                        "m_Enabled": 1,
                        "m_CastShadows": 1,
                        "m_RenderingLayerMask": 1,
                        "m_Materials": [copy.deepcopy(materials.get(s, {"fileID": 0}))
                                        for s in slots],
                    }))
            if index == 0 and info.get("clips"):
                node.components.append(RComp(
                    key ^ 0x95, uy.ANIMATOR, "Animator",
                    {"m_Enabled": 1, "m_Controller": {"fileID": 0},
                     "m_ApplyRootMotion": 0, "_model": guid}))
            if not whole:
                for child_index, child in enumerate(nodes):
                    if child.get("parent") == index:
                        h = hash((guid, child["name"], child_index)) & MASK
                        c = make(child_index, h, h ^ 1, h ^ 4)
                        c.parent = node
                        node.children.append(c)
            node.model_node = (guid, "" if whole else src["name"])
            return node

        root = make(0, FBX_ROOT_GAME_OBJECT, FBX_ROOT_GAME_OBJECT, FBX_ROOT_TRANSFORM)
        root.key = FBX_ROOT_GAME_OBJECT
        return root

    def model_materials(self, path, info):
        """{material slot: reference} for a model's remapped materials."""
        result = {}
        names = [m.get("name") for m in info.get("materials", [])]
        remaps = {}
        meta = path + ".meta"
        if os.path.exists(meta):
            import yaml
            with open(meta, encoding="utf-8", errors="replace") as f:
                try:
                    data = yaml.load(f, Loader=uy._Loader) or {}
                except Exception:
                    data = {}
            importer = data.get("ModelImporter", {}) or {}
            for item in importer.get("externalObjects") or []:
                first = item.get("first") or {}
                if first.get("type") == "UnityEngine:Material":
                    remaps[first.get("name")] = item.get("second")
        for slot, name in enumerate(names):
            if name in remaps and remaps[name]:
                result[slot] = remaps[name]
            else:
                result[slot] = {"fileID": 0, "_embedded": name}
        return result

    # ------------------------------------------------------- prefabs --

    def resolve_file(self, path, guid=None):
        """Roots of a scene or prefab file, with instances expanded."""
        objects = self.parse(path)
        nodes = {}       # game object id -> RNode
        by_key = {}      # any object id -> (RNode, RComp or None)
        transforms = {}  # transform id -> RNode

        for obj in objects.values():
            if obj.cls == uy.GAME_OBJECT and not obj.stripped:
                d = obj.data
                node = RNode(obj.file_id, d.get("m_Name") or "GameObject")
                node.go_key = obj.file_id
                node.layer = int(d.get("m_Layer", 0) or 0)
                node.tag = d.get("m_TagString") or "Untagged"
                node.active = bool(d.get("m_IsActive", 1))
                nodes[obj.file_id] = node
                by_key[obj.file_id] = (node, None)

        # Components on this file's own game objects.
        for obj in objects.values():
            if obj.stripped or obj.cls in (uy.GAME_OBJECT, uy.PREFAB_INSTANCE):
                continue
            go_id, _ = uy.ref(obj.data.get("m_GameObject"))
            if go_id in nodes:
                node = nodes[go_id]
                comp = RComp(obj.file_id, obj.cls, obj.type_name, obj.data)
                if obj.cls in uy.TRANSFORM_CLASSES:
                    node.transform = comp
                    transforms[obj.file_id] = node
                by_key[obj.file_id] = (node, comp)
        for node in nodes.values():
            comps = []
            go = objects[node.go_key].data
            for entry in go.get("m_Component") or []:
                cid, _ = uy.ref(entry.get("component"))
                if cid in by_key and by_key[cid][1] is not None and by_key[cid][1] is not node.transform:
                    comps.append(by_key[cid][1])
            node.components = comps

        # Expand prefab instances.
        instances = {}
        for obj in objects.values():
            if obj.cls != uy.PREFAB_INSTANCE:
                continue
            roots = self.expand_instance(obj, objects)
            if roots is None:
                continue
            instances[obj.file_id] = roots
            for root in roots:
                for n in root.walk():
                    by_key[n.key] = (n, None)
                    if n.go_key is not None:
                        by_key[n.go_key] = (n, None)
                    if n.transform:
                        by_key[n.transform.key] = (n, n.transform)
                        transforms[n.transform.key] = n
                    for c in n.components:
                        by_key[c.key] = (n, c)

        # Stripped stubs stand for objects of an instance; Unity does not
        # always number them (instance ^ source), so map them explicitly.
        aliases = {}
        for obj in objects.values():
            if not obj.stripped:
                continue
            inst, _ = uy.ref(obj.data.get("m_PrefabInstance"))
            source, _ = uy.ref(obj.data.get("m_CorrespondingSourceObject"))
            if inst and source:
                real = nested_id(inst, source)
                aliases[obj.file_id] = real
                if real in by_key:
                    by_key[obj.file_id] = by_key[real]
                    if real in transforms:
                        transforms[obj.file_id] = transforms[real]

        # Components this file adds to objects of its instances (their
        # m_GameObject is a stripped stub).
        for obj in objects.values():
            if obj.stripped or obj.cls in (uy.GAME_OBJECT, uy.PREFAB_INSTANCE):
                continue
            go_id, _ = uy.ref(obj.data.get("m_GameObject"))
            stub = objects.get(go_id)
            if stub is None or not stub.stripped:
                continue
            target = by_key.get(go_id)
            if target is None:
                continue
            node = target[0]
            comp = RComp(obj.file_id, obj.cls, obj.type_name, obj.data)
            node.components.append(comp)
            by_key[obj.file_id] = (node, comp)

        # Parent/child links of this file's transforms.
        def resolve_transform(tid):
            if tid in transforms:
                return transforms[tid]
            entry = by_key.get(tid)
            return entry[0] if entry else None

        # Each of this file's transforms hangs under its m_Father (which may
        # be the stripped stub of an instance's object).
        child_lists = {}
        for tid, node in list(transforms.items()):
            if node.transform is None or node.transform.key != tid:
                continue
            obj = objects.get(tid)
            if obj is None or obj.stripped:
                continue
            father_id, _ = uy.ref(node.transform.data.get("m_Father"))
            if father_id:
                parent = resolve_transform(father_id)
                if parent is not None and parent is not node and node not in parent.children:
                    if node.parent is not None and node in node.parent.children:
                        node.parent.children.remove(node)
                    node.parent = parent
                    parent.children.append(node)
            kids = []
            for entry in node.transform.data.get("m_Children") or []:
                cid, _ = uy.ref(entry)
                child = resolve_transform(cid)
                if child is not None:
                    kids.append(child)
            child_lists[node] = kids

        # Instances parented to a transform of their own source (added
        # under objects of another instance) or to this file's transforms.
        for inst_id, roots in instances.items():
            parent_id, _ = uy.ref(
                objects[inst_id].data.get("m_Modification", {}).get("m_TransformParent"))
            parent = resolve_transform(parent_id) if parent_id else None
            for root in roots:
                if root.parent is not None:
                    continue
                if parent is not None and root not in parent.children:
                    root.parent = parent
                    parent.children.append(root)

        # Roots, in scene order where Unity records it.
        roots = []
        for obj in objects.values():
            if obj.cls == uy.SCENE_ROOTS:
                for entry in obj.data.get("m_Roots") or []:
                    rid, _ = uy.ref(entry)
                    node = resolve_transform(rid)
                    if node is not None and node.parent is None and node not in roots:
                        roots.append(node)
        for node in list(nodes.values()):
            if node.parent is None and node.transform is not None and node not in roots:
                roots.append(node)
        for inst_roots in instances.values():
            for root in inst_roots:
                if root.parent is None and root not in roots:
                    roots.append(root)
        # Children in m_Children order (prefab instance roots included).
        for node, kids in child_lists.items():
            ordered = [k for k in kids if k in node.children]
            ordered += [k for k in node.children if k not in ordered]
            node.children = ordered
        # References to stubs now point at the objects they stand for.
        if aliases:
            alias = lambda file_id: aliases.get(file_id, file_id)
            for root in roots:
                for n in root.walk():
                    if n.transform:
                        remap_refs(n.transform.data, alias)
                    for c in n.components:
                        remap_refs(c.data, alias)
        return roots

    def expand_instance(self, obj, objects):
        mod = obj.data.get("m_Modification") or {}
        source_id, source_guid = uy.ref(obj.data.get("m_SourcePrefab"))
        if not source_guid:
            return None
        roots = self.resolve_guid(source_guid)
        if not roots:
            self.warn(f"prefab instance {obj.file_id}: source {source_guid} "
                      f"({self.db.path(source_guid)}) could not be loaded")
            return None
        index = {}
        for root in roots:
            for n in root.walk():
                index[n.key] = (n, None)
                if n.go_key is not None:
                    index[n.go_key] = (n, None)
                if n.transform:
                    index[n.transform.key] = (n, n.transform)
                for c in n.components:
                    index[c.key] = (n, c)

        # Removed components and game objects.
        removed = set()
        for entry in mod.get("m_RemovedComponents") or []:
            rid, _ = uy.ref(entry)
            removed.add(rid)
        for root in roots:
            for n in root.walk():
                n.components = [c for c in n.components if c.key not in removed]
        removed_gos = set()
        for entry in mod.get("m_RemovedGameObjects") or []:
            rid, _ = uy.ref(entry)
            removed_gos.add(rid)
        if removed_gos:
            for root in roots:
                for n in list(root.walk()):
                    n.children = [c for c in n.children if c.go_key not in removed_gos]

        for m in mod.get("m_Modifications") or []:
            tid, _tguid = uy.ref(m.get("target"))
            path = m.get("propertyPath") or ""
            value = m.get("value")
            reference = m.get("objectReference")
            if isinstance(reference, dict) and (reference.get("fileID") or reference.get("guid")):
                value = reference
            target = index.get(tid)
            if target is None:
                target = self.guess_target(roots, path)
                if target is None:
                    continue
            node, comp = target
            self.apply_modification(node, comp, path, value)

        for root in roots:
            root.rekey(obj.file_id)
            root.instance_guid = source_guid
            root.instance_key = obj.file_id
        return roots

    def guess_target(self, roots, path):
        """Finds the object a modification of an unknown (hashed) id means."""
        root = roots[0]
        head = path.split(".")[0]
        if head in ("m_Layer",):
            return (root, None)  # applied to the whole model below
        if head in ("m_LocalPosition", "m_LocalRotation", "m_LocalScale",
                    "m_LocalEulerAnglesHint", "m_RootOrder"):
            return (root, root.transform)
        if head in ("m_Controller", "m_Avatar", "m_ApplyRootMotion"):
            for c in root.components:
                if c.cls == uy.ANIMATOR:
                    return (root, c)
        if head == "m_Materials":
            for c in root.components:
                if c.cls == uy.MESH_RENDERER:
                    return (root, c)
        return None

    def apply_modification(self, node, comp, path, value):
        if comp is None:
            if path == "m_Name":
                node.name = str(value)
            elif path == "m_Layer":
                node.layer = int(value)
            elif path == "m_TagString":
                node.tag = str(value)
            elif path == "m_IsActive":
                node.active = bool(int(value))
            return
        uy.set_property(comp.data, path, _coerce(value))


def _coerce(value):
    if isinstance(value, str):
        try:
            if value.strip().lstrip("-").isdigit():
                return int(value)
            return float(value)
        except ValueError:
            return value
    return value
