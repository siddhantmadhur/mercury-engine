"""Writes Mercury's mtext format (.mmap, .mprefab, .masset, .mproj)."""

import math


class NodePath:
    def __init__(self, path):
        self.path = path

    def __eq__(self, other):
        return isinstance(other, NodePath) and other.path == self.path

    def __hash__(self):
        return hash(("NodePath", self.path))


class Resource:
    def __init__(self, path):
        self.path = path

    def __eq__(self, other):
        return isinstance(other, Resource) and other.path == self.path

    def __hash__(self):
        return hash(("Resource", self.path))


class Vec2(tuple):
    pass


class Vec3(tuple):
    pass


class Color(tuple):
    pass


def number(v):
    v = float(v)
    if math.isnan(v) or math.isinf(v):
        v = 0.0
    if abs(v) < 1e-7:
        v = 0.0
    text = repr(round(v, 6))
    if "e" in text or "E" in text:
        text = f"{v:.9f}".rstrip("0").rstrip(".")
    if text.endswith(".0"):
        text = text[:-2]
    return text


def quote(text):
    out = ['"']
    for c in str(text):
        if c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif c == "\n":
            out.append("\\n")
        elif c == "\t":
            out.append("\\t")
        elif c == "\r":
            out.append("\\r")
        else:
            out.append(c)
    out.append('"')
    return "".join(out)


def value(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        text = number(v)
        return text if "." in text else text + ".0"
    if isinstance(v, str):
        return quote(v)
    if isinstance(v, Vec2):
        return f"Vector2({number(v[0])}, {number(v[1])})"
    if isinstance(v, Vec3):
        return f"Vector3({number(v[0])}, {number(v[1])}, {number(v[2])})"
    if isinstance(v, Color):
        return f"Color({number(v[0])}, {number(v[1])}, {number(v[2])}, {number(v[3])})"
    if isinstance(v, NodePath):
        return f"NodePath({quote(v.path)})"
    if isinstance(v, Resource):
        return f"Resource({quote(v.path)})"
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(value(x) for x in v) + "]"
    raise TypeError(f"cannot write {v!r}")


def section(tag, attributes=(), properties=()):
    head = "[" + tag
    for key, v in attributes:
        head += f" {key}={value(v)}"
    head += "]"
    lines = [head]
    for key, v in properties:
        lines.append(f"{key} = {value(v)}")
    return "\n".join(lines)


def document(sections):
    return "\n\n".join(sections) + "\n"


def asset(type_name, properties):
    return document([section("masset", [("type", type_name), ("format", 1)],
                             list(properties.items()) if isinstance(properties, dict)
                             else properties)])
