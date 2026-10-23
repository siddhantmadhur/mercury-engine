"""Reads Unity's YAML scene and prefab files.

A Unity file is a stream of documents, each headed

    --- !u!<class id> &<file id>[ stripped]

followed by one mapping ({TypeName: {fields...}}). Objects reference each
other with {fileID: N} (same file) or {fileID: N, guid: G, type: T}
(another asset).
"""

import re

import yaml

try:
    _Loader = yaml.CSafeLoader
except AttributeError:  # PyYAML without libyaml
    _Loader = yaml.SafeLoader

_HEADER = re.compile(r"^--- !u!(\d+) &(-?\d+)( stripped)?\s*$", re.M)

# Class ids used by the importer.
GAME_OBJECT = 1
TRANSFORM = 4
CAMERA = 20
MESH_RENDERER = 23
MESH_FILTER = 33
RIGIDBODY = 54
MESH_COLLIDER = 64
BOX_COLLIDER = 65
AUDIO_LISTENER = 81
AUDIO_SOURCE = 82
ANIMATOR = 95
RENDER_SETTINGS = 104
LIGHT = 108
MONO_BEHAVIOUR = 114
SPHERE_COLLIDER = 135
CAPSULE_COLLIDER = 136
SKINNED_MESH_RENDERER = 137
CHARACTER_CONTROLLER = 143
RECT_TRANSFORM = 224
PREFAB_INSTANCE = 1001
SCENE_ROOTS = 1660057539

TRANSFORM_CLASSES = (TRANSFORM, RECT_TRANSFORM)


class UnityObject:
    __slots__ = ("cls", "file_id", "stripped", "type_name", "data")

    def __init__(self, cls, file_id, stripped, type_name, data):
        self.cls = cls
        self.file_id = file_id
        self.stripped = stripped
        self.type_name = type_name
        self.data = data

    def __repr__(self):
        return f"<{self.type_name} {self.file_id}{' stripped' if self.stripped else ''}>"


def parse_file(path):
    """Returns {file_id: UnityObject} for every document of a Unity file."""
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    return parse_text(text)


def parse_text(text):
    objects = {}
    headers = list(_HEADER.finditer(text))
    for i, header in enumerate(headers):
        start = header.end()
        end = headers[i + 1].start() if i + 1 < len(headers) else len(text)
        body = text[start:end]
        try:
            doc = yaml.load(body, Loader=_Loader)
        except yaml.YAMLError:
            doc = yaml.load(_sanitize(body), Loader=_Loader)
        if not isinstance(doc, dict) or not doc:
            continue
        type_name, data = next(iter(doc.items()))
        obj = UnityObject(int(header.group(1)), int(header.group(2)),
                          bool(header.group(3)), type_name, data or {})
        objects[obj.file_id] = obj
    return objects


def _sanitize(body):
    # Unity sometimes writes values YAML cannot parse unquoted (e.g. a
    # string starting with '*' or '%'); quote them.
    lines = []
    for line in body.splitlines():
        m = re.match(r"^(\s*[\w\.\[\]]+: )([*%@`].*)$", line)
        if m:
            line = m.group(1) + "'" + m.group(2).replace("'", "''") + "'"
        lines.append(line)
    return "\n".join(lines)


def read_meta_guid(meta_path):
    with open(meta_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("guid:"):
                return line.split(":", 1)[1].strip()
    return None


def ref(value):
    """(file_id, guid) of a reference value, or (0, None)."""
    if isinstance(value, dict):
        return int(value.get("fileID", 0) or 0), value.get("guid")
    return 0, None


# ---------------------------------------------------------- property paths --

_INDEX = re.compile(r"^data\[(\d+)\]$")


def set_property(data, path, value):
    """Applies a prefab modification ("a.b.Array.data[2].c") to a dict."""
    parts = path.split(".")
    node = data
    i = 0
    while i < len(parts):
        part = parts[i]
        last = i == len(parts) - 1
        if part == "Array":
            # "Array.size" or "Array.data[N]"
            nxt = parts[i + 1]
            if not isinstance(node, list):
                return
            if nxt == "size":
                n = int(value)
                while len(node) < n:
                    node.append(None)
                del node[n:]
                return
            m = _INDEX.match(nxt)
            if not m:
                return
            index = int(m.group(1))
            while len(node) <= index:
                node.append(None)
            if i + 1 == len(parts) - 1:
                node[index] = value
                return
            if not isinstance(node[index], (dict, list)):
                node[index] = {}
            node = node[index]
            i += 2
            continue
        if not isinstance(node, dict):
            return
        if last:
            node[part] = value
            return
        nxt_is_array = i + 1 < len(parts) and parts[i + 1] == "Array"
        if part not in node or not isinstance(node[part], (dict, list)):
            node[part] = [] if nxt_is_array else {}
        node = node[part]
        i += 1


def get_property(data, path, default=None):
    node = data
    for part in path.split("."):
        if isinstance(node, dict) and part in node:
            node = node[part]
        else:
            return default
    return node
