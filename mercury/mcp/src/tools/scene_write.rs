// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The scene write tool group: edits to the open map, its selection, saving
//! and undo.
//!
//! The handlers are in `editor/mcp/tools/scene_write.cc`.
//!
//! - `apply_scene_operations` (Write): a batch of operations applied as one
//!   undo step, all or nothing.
//! - `select_node` (Read): selection is editor UI state, not map data.
//! - `save_map` (Write): saves the open map.
//! - `undo` / `redo` (Write): step through the editor's undo history.
//!
//! The argument types below are the contract with the C++ parser: every field
//! name is read verbatim by `scene_write.cc`, and the `///` comments are what
//! an agent reads in the tool's input schema.

// The argument types are read for their schema (and by the tests), never
// built in Rust: the C++ handler parses the arguments.
#![allow(dead_code)]

use schemars::JsonSchema;
use serde::Deserialize;

use super::{Mode, NodeSelector, ToolSpec, schema};

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![
        apply_scene_operations(),
        select_node(),
        save_map(),
        undo(),
        redo(),
    ]
}

/// A node reference inside `apply_scene_operations`: the shared
/// [`NodeSelector`] (a `uid` or a `path`) plus an `alias` naming a node created
/// earlier in the same batch. `uid` and `path` are documented on
/// [`NodeSelector`]; alias resolution stays in the C++ handler.
#[derive(Deserialize, JsonSchema)]
pub struct OpNodeSelector {
    #[serde(flatten)]
    pub selector: NodeSelector,
    /// The `id` an earlier `create` or `instantiate_prefab` operation of the
    /// SAME batch gave its node. Only valid inside `apply_scene_operations`.
    pub alias: Option<String>,
}

/// A node's local transform. Every field is optional; only the ones given are
/// set. Local means relative to the parent node.
///
/// Conventions (from the engine): right-handed, +Y up, a camera or light
/// looks along its local -Z. Positions are in world units, a convention rather
/// than an enforced unit: physics treats them as meters (gravity defaults to
/// 9.81). Rotations are Euler angles in degrees about X, Y and Z,
/// applied X first, then Y, then Z (the matrix is Rz * Ry * Rx).
#[derive(Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct TransformArgs {
    /// Local position [x, y, z] in world units (meters to the physics),
    /// relative to the parent.
    pub position: Option<[f64; 3]>,
    /// Local rotation as Euler angles [x, y, z] in DEGREES, applied X, then
    /// Y, then Z. Give this or `rotation_quat`, not both.
    pub rotation_euler_degrees: Option<[f64; 3]>,
    /// Local rotation as a quaternion [x, y, z, w] (normalized for you; it
    /// must not be all zeros). It is stored as Euler degrees. Give this or
    /// `rotation_euler_degrees`, not both.
    pub rotation_quat: Option<[f64; 4]>,
    /// Local scale [x, y, z]; 1 is the original size (a cube, sphere or quad
    /// is 1 unit across at scale 1).
    pub scale: Option<[f64; 3]>,
}

/// A component to put on a node: its type and the properties to set.
#[derive(Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ComponentArgs {
    /// The component type name, like "MeshRenderer", "Rigidbody" or a game
    /// script's class name. Unknown names are rejected with a suggestion; the
    /// engine's types and their fields are listed by the schema tools.
    #[serde(rename = "type")]
    pub component_type: String,
    /// Property values by field name, in the same JSON encoding the scene
    /// read tools return: bool, int, float, string and enum values as
    /// themselves; Vector2/Vector3/Color as [x, y], [x, y, z] and
    /// [r, g, b, a]; a node path field as {"path": "Child/Grandchild"}; an
    /// asset field as {"res": "res://assets/x.masset"} (the file must exist);
    /// a list as an array. Fields left out keep their defaults. Unknown field
    /// names are rejected with a suggestion.
    pub props: Option<serde_json::Map<String, serde_json::Value>>,
}

/// One scene operation. `op` picks the kind; every other field belongs to
/// that kind.
#[derive(Deserialize, JsonSchema)]
#[serde(tag = "op", rename_all = "snake_case", deny_unknown_fields)]
pub enum Operation {
    /// Create a new node (with a Transform component) and optionally give it
    /// components.
    Create {
        /// The new node's name. Slashes, quotes and backslashes are removed
        /// ("." and ".." become "Node"); if a sibling already has the name, a
        /// number is appended (the result reports the final path and a
        /// warning).
        name: String,
        /// The parent node. Defaults to the map's root.
        parent: Option<OpNodeSelector>,
        /// The node's local transform. Defaults to the origin, no rotation,
        /// scale 1.
        transform: Option<TransformArgs>,
        /// Components to add (the Transform already exists; set it with
        /// `transform`, not here).
        components: Option<Vec<ComponentArgs>>,
        /// An alias for this node that LATER operations of the same batch can
        /// use as {"alias": "<id>"} (as `node`, `parent` or `new_parent`),
        /// and that the result echoes. Must be unique in the batch.
        id: Option<String>,
    },
    /// Place a copy of a prefab (an .mprefab file) as a new node with its
    /// children.
    InstantiatePrefab {
        /// The prefab's res:// path, like "res://prefabs/crate.mprefab". The
        /// file must exist and load.
        prefab: String,
        /// The parent node. Defaults to the map's root.
        parent: Option<OpNodeSelector>,
        /// The instance's name. Defaults to the prefab file's name without
        /// the extension.
        name: Option<String>,
        /// A transform override for the instance's root node (local, relative
        /// to the parent).
        transform: Option<TransformArgs>,
        /// An alias for the instance's root node (see `create`).
        id: Option<String>,
    },
    /// Delete a node and all its children. The root cannot be deleted.
    Delete {
        /// The node to delete.
        node: OpNodeSelector,
    },
    /// Move a node (with its children) under another parent.
    Reparent {
        /// The node to move. The root cannot be moved.
        node: OpNodeSelector,
        /// The new parent. It must not be the node itself or one of its
        /// descendants.
        new_parent: OpNodeSelector,
        /// When true (the default), the node's local transform is rewritten
        /// so it keeps its place in the world: position, rotation and scale
        /// are re-expressed relative to the new parent (a parent's rotation
        /// and scale apply to its children, as the engine composes them:
        /// world position = parent position + parent rotation * (parent scale
        /// * local position)). When false, the local transform is kept as it
        /// is, so the node moves with its new parent. If a sibling of the new
        /// parent already has the node's name, a number is appended.
        keep_world_transform: Option<bool>,
    },
    /// Set parts of a node's local transform; the ones left out are not
    /// changed. Give at least one.
    SetTransform {
        /// The node to change.
        node: OpNodeSelector,
        /// Local position [x, y, z] in world units (meters to the physics).
        position: Option<[f64; 3]>,
        /// Local rotation as Euler angles [x, y, z] in DEGREES, applied X,
        /// then Y, then Z. Give this or `rotation_quat`, not both.
        rotation_euler_degrees: Option<[f64; 3]>,
        /// Local rotation as a quaternion [x, y, z, w] (normalized for you).
        /// Give this or `rotation_euler_degrees`, not both.
        rotation_quat: Option<[f64; 4]>,
        /// Local scale [x, y, z].
        scale: Option<[f64; 3]>,
    },
    /// Set one property of a component the node already has.
    SetProperty {
        /// The node to change.
        node: OpNodeSelector,
        /// The component's type name; the node must have it (use
        /// `add_component` first otherwise).
        component: String,
        /// The field name; it must exist on that component type.
        field: String,
        /// The new value, in the encoding described on `ComponentArgs.props`
        /// (bool, number, string, enum string, [x, y(, z(, w))] arrays,
        /// {"path": ".."}, {"res": "res://.."}, arrays for lists, null for
        /// an unset reference). A value equal to the field's default removes
        /// the stored property.
        value: serde_json::Value,
    },
    /// Add a component to a node. A node holds one component of each type.
    AddComponent {
        /// The node to change.
        node: OpNodeSelector,
        /// The component type name. The node must not have it yet.
        #[serde(rename = "type")]
        component_type: String,
        /// Initial property values by field name (see `ComponentArgs.props`).
        props: Option<serde_json::Map<String, serde_json::Value>>,
    },
    /// Remove a component from a node. The Transform cannot be removed.
    RemoveComponent {
        /// The node to change.
        node: OpNodeSelector,
        /// The component type name; the node must have it.
        #[serde(rename = "type")]
        component_type: String,
    },
}

/// `apply_scene_operations`'s arguments.
#[derive(Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ApplySceneOperationsArgs {
    /// The operations, applied in order (1 to 1000). Later operations can
    /// refer to nodes created by earlier ones with {"alias": "<id>"}.
    pub operations: Vec<Operation>,
    /// A node to select in the editor once the batch succeeded (a uid, path
    /// or alias).
    pub select: Option<OpNodeSelector>,
}

/// `select_node`'s arguments.
#[derive(Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct SelectNodeArgs {
    /// The node to select in the editor's hierarchy and inspector (a uid or a
    /// path).
    pub node: NodeSelector,
}

/// `save_map`'s arguments: none.
#[derive(Deserialize, JsonSchema)]
pub struct SaveMapArgs {}

/// `undo`'s arguments: none.
#[derive(Deserialize, JsonSchema)]
pub struct UndoArgs {}

/// `redo`'s arguments: none.
#[derive(Deserialize, JsonSchema)]
pub struct RedoArgs {}

fn apply_scene_operations() -> ToolSpec {
    ToolSpec::new(
        "apply_scene_operations",
        "Edits the open map with a batch of operations, in order: create, \
         instantiate_prefab, delete, reparent, set_transform, set_property, \
         add_component and remove_component. Use it for every scene change; \
         prefer one batch of many operations over many calls. The batch is \
         ALL OR NOTHING: every operation is checked first (node references, \
         component types and fields, values, prefab files), and if any fails \
         nothing is changed and the error names the failing operation by its \
         index in `operations` (0-based), with a did-you-mean `suggestion` for \
         an unknown name. A successful batch is ONE undo step in the editor \
         (the `undo` tool or Ctrl+Z reverts it whole). The map is changed in \
         the editor but not saved to disk: call `save_map` for that. \
         To refer to a node created earlier in the same batch, give the \
         creating operation an `id` and use {\"alias\": \"<id>\"} wherever a \
         node reference is expected; other nodes are {\"uid\": n} or \
         {\"path\": \"Root/Child\"}. \
         Conventions: positions are in world units (physics treats them as meters, \
         gravity 9.81); the world is right-handed with +Y up \
         and a camera or light looks along its local -Z; positions, rotations \
         and scales are LOCAL (relative to the parent); rotations are Euler \
         angles in degrees applied X, then Y, then Z (or a quaternion \
         [x, y, z, w]). \
         Returns {\"applied\": <operation count>, \"created\": [{\"op\": \
         <index>, \"alias\"?: \"<id>\", \"uid\": n, \"path\": \"Root/Name\"}], \
         \"warnings\": [..]} and, with `select`, {\"selected\": <uid>}. \
         Read the result's `path`: a created node's name can differ from the \
         requested one when a sibling already had it.",
        Mode::Write,
        || schema::<ApplySceneOperationsArgs>(),
    )
}

fn select_node() -> ToolSpec {
    ToolSpec::new(
        "select_node",
        "Selects a node in the editor's hierarchy and inspector, the way a \
         click does. It only changes the editor's selection (UI state), never \
         the map, so it is allowed on a read-only server. Takes {\"node\": \
         {\"uid\": n}} or {\"node\": {\"path\": \"Root/Child\"}}. Returns \
         {\"selected\": <the node as JSON: name, uid, path, node_path, \
         parent_uid, child_count and its local transform>}. An unknown node \
         is an error with a did-you-mean `suggestion`.",
        Mode::Read,
        || schema::<SelectNodeArgs>(),
    )
}

fn save_map() -> ToolSpec {
    ToolSpec::new(
        "save_map",
        "Saves the open map to its .mmap file in the project (what Ctrl+S \
         does for the map). Call it after `apply_scene_operations` to keep \
         the changes. Returns {\"path\": \"res://maps/main.mmap\"}. Fails \
         when no project or map is open or the file cannot be written.",
        Mode::Write,
        || schema::<SaveMapArgs>(),
    )
}

fn undo() -> ToolSpec {
    ToolSpec::new(
        "undo",
        "Undoes the editor's last change to the map (one whole \
         `apply_scene_operations` batch, or one edit made in the UI). With \
         nothing to undo it changes nothing and reports \"undone\": false. \
         Returns {\"undone\": bool, \"can_undo\": bool, \"can_redo\": bool}.",
        Mode::Write,
        || schema::<UndoArgs>(),
    )
}

fn redo() -> ToolSpec {
    ToolSpec::new(
        "redo",
        "Redoes the change `undo` reverted. With nothing to redo it changes \
         nothing and reports \"redone\": false. Any new edit clears what \
         could be redone. Returns {\"redone\": bool, \"can_undo\": bool, \
         \"can_redo\": bool}.",
        Mode::Write,
        || schema::<RedoArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn spec(name: &str) -> ToolSpec {
        tools().into_iter().find(|t| t.name == name).expect("spec exists")
    }

    #[test]
    fn modes_follow_the_read_only_gate() {
        assert_eq!(spec("apply_scene_operations").mode, Mode::Write);
        assert_eq!(spec("save_map").mode, Mode::Write);
        assert_eq!(spec("undo").mode, Mode::Write);
        assert_eq!(spec("redo").mode, Mode::Write);
        // Selection is UI state, not map data.
        assert_eq!(spec("select_node").mode, Mode::Read);
    }

    #[test]
    fn every_schema_field_has_a_description() {
        // Walks the whole schema: every property, in every variant and $def,
        // must carry a description an agent can read.
        fn walk(path: &str, value: &serde_json::Value, missing: &mut Vec<String>) {
            if let Some(props) = value.get("properties").and_then(|p| p.as_object()) {
                for (name, prop) in props {
                    let described = prop.get("description").is_some()
                        // A `$ref` or `allOf` wrapper keeps the description on
                        // the referenced definition.
                        || prop.get("$ref").is_some()
                        || prop.get("allOf").is_some()
                        // The `op` discriminator of each variant: a constant
                        // naming the variant, documented on the variant.
                        || prop.get("const").is_some();
                    if !described {
                        missing.push(format!("{path}.{name}"));
                    }
                }
            }
            match value {
                serde_json::Value::Object(map) => {
                    for (key, child) in map {
                        walk(&format!("{path}/{key}"), child, missing);
                    }
                }
                serde_json::Value::Array(items) => {
                    for (i, child) in items.iter().enumerate() {
                        walk(&format!("{path}[{i}]"), child, missing);
                    }
                }
                _ => {}
            }
        }
        for tool in tools() {
            let schema = serde_json::Value::Object(tool.schema());
            let mut missing = Vec::new();
            walk(tool.name, &schema, &mut missing);
            assert!(missing.is_empty(), "undocumented fields: {missing:?}");
        }
    }

    #[test]
    fn the_op_enum_is_tagged_by_op() {
        let schema = serde_json::Value::Object(spec("apply_scene_operations").schema());
        let text = schema.to_string();
        for op in [
            "create",
            "instantiate_prefab",
            "delete",
            "reparent",
            "set_transform",
            "set_property",
            "add_component",
            "remove_component",
        ] {
            assert!(
                text.contains(&format!("\"const\":\"{op}\"")),
                "the schema lists the {op} operation: {text}"
            );
        }
        assert!(text.contains("keep_world_transform"));
        assert!(text.contains("alias"));
    }

    #[test]
    fn operations_parse_the_way_the_cpp_side_reads_them() {
        let args = serde_json::from_value::<ApplySceneOperationsArgs>(json!({
            "operations": [
                {"op": "create", "name": "Crate", "parent": {"path": "Root"},
                 "transform": {"position": [1, 2, 3], "rotation_euler_degrees": [0, 90, 0]},
                 "components": [{"type": "MeshRenderer", "props": {"mesh": "cube"}}],
                 "id": "crate"},
                {"op": "instantiate_prefab", "prefab": "res://p.mprefab"},
                {"op": "reparent", "node": {"alias": "crate"}, "new_parent": {"uid": 7},
                 "keep_world_transform": false},
                {"op": "set_transform", "node": {"alias": "crate"}, "rotation_quat": [0, 0, 0, 1]},
                {"op": "set_property", "node": {"uid": 7}, "component": "Transform",
                 "field": "position", "value": [0, 1, 0]},
                {"op": "add_component", "node": {"uid": 7}, "type": "Rigidbody"},
                {"op": "remove_component", "node": {"uid": 7}, "type": "Rigidbody"},
                {"op": "delete", "node": {"path": "Root/Crate"}}
            ],
            "select": {"alias": "crate"}
        }));
        let args = args.expect("a valid batch parses");
        assert_eq!(args.operations.len(), 8);
        assert!(args.select.is_some());
        let bad = serde_json::from_value::<ApplySceneOperationsArgs>(json!({
            "operations": [{"op": "explode", "node": {"uid": 1}}]
        }));
        assert!(bad.is_err(), "an unknown op is rejected");
    }
}
