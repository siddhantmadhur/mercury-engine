// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * scene_json.h
 *
 * Scene helpers shared by the MCP tool groups: finding a node from a client's
 * reference, describing a node as JSON, and turning a client's JSON back into
 * a property value. Every tool that reads or edits the open map goes through
 * these, so a node and a value look the same in every tool.
 *
 * INTERNAL header (it includes nlohmann/json): it is excluded from the SDK
 * install, like mcp/tool_registry.h.
 *
 * Value encoding. Values use the introspection API's encoding
 * (introspection::VariantToJson, shared via introspection/variant_json.h):
 *
 *   bool, int, float, string, enum   the matching JSON type
 *   Vector2 / Vector3 / Color        [x, y] / [x, y, z] / [r, g, b, a]
 *   NodePath (a NodeRef field)       {"path": "Child/Grandchild"}
 *   Resource (an asset field)        {"res": "res://assets/x.masset"}
 *   list                             a JSON array of the element encoding
 *   event                            [[{"path": ".."}, "Component", "method"
 *                                      [, argument]], ...]
 *
 * JsonToPropertyValue also accepts the tagged forms {"vec2": [..]},
 * {"vec3": [..]}, {"color": [..]} for vectors and colors, a bare string for
 * NodePath and Resource fields, and null for an unset reference.
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_SCENE_JSON_H
#define MERCURY_EDITOR_MCP_TOOLS_SCENE_JSON_H

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "project/map.h"
#include "scripting/reflection.h"
#include "serialization/mtext.h"

namespace Mercury {

class EditorScene;
class Project;

}  // namespace Mercury

namespace Mercury::mcp {

/**
 * @brief A node reference as a client writes it: {"uid": 42}, {"uid": "42"}
 * or {"path": "Root/Child/Grandchild"}, parsed
 *
 * Not the engine's Mercury::NodeRef, which is a path stored in a component
 * field; this is only the way a tool call names a node.
 */
struct NodeSelector {
  // Set when the client used the uid form (a JSON number or decimal string).
  std::optional<uint64_t> uid;
  // Set when the client used the path form, as given.
  std::string path;

  // Parses `ref` without looking at any map. False, with `error` saying what
  // is wrong with the reference, when it is neither form (or both).
  static bool Parse(const nlohmann::json& ref, NodeSelector* out,
                    std::string* error);
};

/**
 * @brief The node a reference names in the open map
 *
 * The path form is the node's full path starting at the root's name
 * ("Root/Child/Grandchild"); the engine's own form without the root
 * ("Child/Grandchild", "." for the root) is accepted too.
 *
 * @return nullptr and fills `error` when the reference is malformed or names
 *         no node. The error says which form failed ("node ref {\"path\":
 *         ...} not found ...") and, when a close name exists, ends with
 *         `(did you mean "...")` from introspection::Suggest. `suggestion`
 *         (optional) then receives the bare candidate, for tools that return
 *         {"error": ..., "suggestion": ...}; it is left empty otherwise.
 */
MapNode* ResolveNode(EditorScene& scene, const nlohmann::json& ref,
                     std::string* error, std::string* suggestion = nullptr);

/** The node's full path from the root's name: "Root/Child/Grandchild". */
std::string FullNodePath(const MapNode& node);

/**
 * @brief A node as JSON
 *
 *   name, uid, path (full, "Root/Child"), node_path (the engine's form, "."
 *   for the root, for NodePath values), parent_uid (null for the root),
 *   child_count, and transform: the node's local Transform as
 *   {"position": [x, y, z], "rotation": [x, y, z, w] (quaternion),
 *    "euler_degrees": [x, y, z], "scale": [x, y, z]}.
 *
 * With `include_components`, also "components": [{"type": "...",
 * "props": {field: value, ...}}, ...] in node order; props are exactly the
 * properties stored on the node (fields left at their default are absent),
 * in the encoding described at the top of this file.
 */
nlohmann::json NodeToJson(EditorScene& scene, const MapNode& node,
                          bool include_components);

/**
 * @brief Turns a client's JSON into the value for one component field
 *
 * The inverse of the property encoding in NodeToJson: it checks `json`
 * against `field.type` (and, for enums, `field.enum_values`; for lists, each
 * item against `field.element_type`; for events, the shape of each call) and
 * fills `out` with the Variant a Map stores. Values the engine would reject
 * are rejected here: wrong JSON type, wrong array length, non-finite numbers,
 * an enum string outside the list, an int with a fractional part.
 *
 * A Resource field must name a file that exists in `project` when one is
 * given (a res:// path; "" and null mean unset and always pass). The asset
 * type is not checked beyond that. With `project` null the existence check is
 * skipped.
 *
 * @return false and fills `error` (which names the field) on a bad value;
 *         `out` is untouched then.
 */
bool JsonToPropertyValue(const FieldInfo& field, const nlohmann::json& json,
                         mtext::Variant& out, std::string* error,
                         const Project* project = nullptr);

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOLS_SCENE_JSON_H
