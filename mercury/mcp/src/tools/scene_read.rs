// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The scene-read tool group: everything an agent needs to look at the open
//! project, map, selection and console without changing anything.
//!
//! All five tools are [`Mode::Read`]; their handlers are in
//! `editor/mcp/tools/scene_read.cc`. The argument structs below are what the
//! handlers read from `args`: keep the field names in step with the C++.
//!
//! The engine conventions the descriptions state, and where they come from:
//!
//! - Transform rotation is Euler angles in degrees applied X, then Y, then Z
//!   (DOCUMENTATION.md, `Transform` row of the component table). A node's
//!   position, rotation and scale are LOCAL values relative to its parent,
//!   and the world transform composes the parent chain: world position =
//!   parent position + parent rotation x (parent scale x local position),
//!   world rotation = parent rotation x local rotation, world scale = parent
//!   scale x local scale (`MapScene::UpdateEntityTransformHierarchy`,
//!   mercury/scene/map_scene.cc:1832-1841, and `OnLocalTransformChanged`,
//!   map_scene.cc:1800-1802; map load does the same at map_scene.cc:770-772).
//!   DOCUMENTATION.md line 136 ("rotation and scale are not inherited") is out
//!   of date, so the descriptions follow the code.
//! - +Y is up and the axes are right-handed: the default gravity is
//!   `(0, -9.81, 0)` (DOCUMENTATION.md, `[[physics]]`) and the Unity importer
//!   mirrors Z to turn Unity's left-handed axes into Mercury's.
//! - There is no physical unit: shapes have unit size at scale 1 (a 1x1x1
//!   cube), gravity is in "units per second squared" (`Project::GetGravity`).
//! - The editor camera's yaw and pitch are degrees of the same Euler
//!   convention (`editor/scene_view.cc`, `EditorScene::CameraForward`): with
//!   both 0 it looks along -Z, and a negative pitch looks downward.

use super::{Mode, NodeSelector, ToolSpec, schema};

// The argument types below exist to generate each tool's input schema (the
// handlers are in C++ and read `args` themselves), so their fields are never
// read in Rust outside the tests.

/// The default and the largest `limit` of every list tool.
const DEFAULT_LIMIT: u32 = 100;
const DEFAULT_CONSOLE_LIMIT: u32 = 200;

fn default_limit() -> u32 {
    DEFAULT_LIMIT
}

fn default_console_limit() -> u32 {
    DEFAULT_CONSOLE_LIMIT
}

fn default_true() -> bool {
    true
}

/// `get_editor_state`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct GetEditorStateArgs {}

/// `get_component_schema`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct GetComponentSchemaArgs {
    /// The exact name of one component or asset type, such as "Rigidbody" or
    /// "Material". Omit it to list every component and asset type (names
    /// only).
    #[serde(default)]
    pub r#type: Option<String>,
}

/// `find_nodes`'s arguments. Every filter that is given must match (they are
/// combined with AND); with none given, every node matches.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct FindNodesArgs {
    /// A glob matched against the node's name only (not its path), ignoring
    /// case: `*` matches any run of characters, `?` one character. For
    /// example "Wall*" or "*_Spawn".
    #[serde(default)]
    pub name_glob: Option<String>,
    /// Only nodes that have a component of this exact type, such as
    /// "Rigidbody" or a script's class name. An unknown type name is an
    /// error with a suggestion.
    #[serde(default)]
    pub component: Option<String>,
    /// Only nodes whose tag equals this string exactly (case-sensitive). A
    /// node has at most one tag.
    #[serde(default)]
    pub tag: Option<String>,
    /// Only nodes whose full path ("Root/Child/Grandchild", as `path` in the
    /// results) starts with this text. It is a plain string prefix: use
    /// "Root/Enemies/" (with the slash) to get a node's descendants without
    /// siblings that merely share the start of its name.
    #[serde(default)]
    pub path_prefix: Option<String>,
    /// The most nodes to return, 1 to 1000. Defaults to 100; values above
    /// 1000 are clamped to 1000. When more nodes match, `truncated` is true.
    #[serde(default = "default_limit")]
    #[schemars(range(min = 1, max = 1000))]
    pub limit: u32,
}

/// `get_node`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct GetNodeArgs {
    /// Which node to describe: {"uid": 42} or {"path": "Root/Player"}.
    pub node: NodeSelector,
    /// Include the node's components with their stored properties. Defaults
    /// to true.
    #[serde(default = "default_true")]
    pub include_components: bool,
    /// Include the node's direct children as [{name, uid}]. Defaults to
    /// false.
    #[serde(default)]
    pub include_children: bool,
}

/// `read_console`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct ReadConsoleArgs {
    /// Where to continue reading: the `next_cursor` of the previous
    /// read_console call, or `console.latest_cursor` from get_editor_state to
    /// read only what is logged from now on. Omit it to get the most recent
    /// `limit` entries. 0 reads from the oldest entry still held.
    #[serde(default)]
    pub cursor: Option<u64>,
    /// Only entries at this level or worse: "info" (everything, the
    /// default), "warning" (warnings and errors) or "error".
    #[serde(default)]
    pub min_level: Option<ConsoleLevel>,
    /// The most entries to return, 1 to 1000. Defaults to 200; values above
    /// 1000 are clamped to 1000. When more match, `truncated` is true and
    /// `next_cursor` continues after the last returned entry.
    #[serde(default = "default_console_limit")]
    #[schemars(range(min = 1, max = 1000))]
    pub limit: u32,
}

/// A console severity.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
#[serde(rename_all = "lowercase")]
pub enum ConsoleLevel {
    Info,
    Warning,
    Error,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![
        get_editor_state(),
        get_component_schema(),
        find_nodes(),
        get_node(),
        read_console(),
    ]
}

fn get_editor_state() -> ToolSpec {
    ToolSpec::new(
        "get_editor_state",
        "Returns a snapshot of the editor: start here to learn what is open. \
         Result: {project: {name, root, main_map} or null when no project is \
         open; open_map: {path (res:// path, \"\" for an unsaved new map), \
         dirty (unsaved changes)}; selection: {node: the selected node \
         (name, uid, path, node_path, parent_uid, child_count, transform; \
         no components, use get_node for those) or null, asset: the selected \
         asset's res:// path or null}; camera: the scene view camera \
         {target: [x, y, z] it orbits around, position: [x, y, z], yaw, pitch \
         (degrees, same Euler convention as node rotation; 0/0 looks along \
         -Z, negative pitch looks down; both 0 in the 2D view), distance \
         (world units from position to target), view_2d}; \
         play_session_running; mcp: {read_only: true when write tools are \
         rejected}; console: {errors, warnings (counts of lines currently \
         held), latest_cursor (pass it to read_console as `cursor` to read \
         only newer lines)}}. Engine conventions: +Y is up, axes are \
         right-handed, positions are in world units (no physical unit: a \
         cube primitive is 1 unit wide at scale 1), and a node's position, \
         rotation and scale are local to its parent (the world transform \
         composes the parent chain).",
        Mode::Read,
        || schema::<GetEditorStateArgs>(),
    )
}

fn get_component_schema() -> ToolSpec {
    ToolSpec::new(
        "get_component_schema",
        "Describes the components and asset types the engine knows, \
         including the open project's scripts: use it before adding a \
         component or setting a property, to learn the exact field names, \
         types, defaults and allowed enum values. Without `type`, returns \
         {components: [{name, category, is_script}], asset_types: [{name, \
         description}]}. With `type` (a component or asset type name), \
         returns that type's full entry: {kind: \"component\" or \
         \"asset_type\", name, description, fields: [{name, type, default, \
         enum_values, asset_type, tooltip, element, runtime_writable}], and \
         for components also category, is_script, removable}. Field values \
         use the same encoding as get_node: Vector2/Vector3/Color as number \
         arrays, a NodePath as {\"path\": \"A/B\"}, a Resource as {\"res\": \
         \"res://...\"}. A Transform's rotation is Euler degrees applied X, \
         then Y, then Z. A name the engine does not know returns an error \
         with a did-you-mean suggestion.",
        Mode::Read,
        || schema::<GetComponentSchemaArgs>(),
    )
}

fn find_nodes() -> ToolSpec {
    ToolSpec::new(
        "find_nodes",
        "Finds nodes of the open map by name, component, tag or path prefix; \
         all the filters given must match. Returns {nodes: [...], truncated} \
         in map order (parents before their children, siblings in order). \
         Each node is {name, uid, path (\"Root/Child\"), node_path, \
         parent_uid (null for the root), child_count, transform (LOCAL, \
         relative to the parent): {position [x, y, z], rotation [x, y, z, w] \
         quaternion, euler_degrees [x, y, z] applied X then Y then Z, scale \
         [x, y, z]}}; \
         components are left out, so call get_node for those. A node's tag \
         is the single string set on the node (Unity's tag). `truncated` is \
         true when more nodes matched than `limit`. Use it to locate nodes \
         before get_node or an edit; with no filters it lists the whole map \
         up to the limit.",
        Mode::Read,
        || schema::<FindNodesArgs>(),
    )
}

fn get_node() -> ToolSpec {
    ToolSpec::new(
        "get_node",
        "Describes one node of the open map. Returns {name, uid, path, \
         node_path, parent_uid, child_count, transform (LOCAL: \
         relative to the parent, whose rotation and scale the world \
         transform composes): {position [x, y, z] in world units (+Y is \
         up, right-handed), rotation [x, y, z, w] quaternion, euler_degrees \
         [x, y, z] in degrees applied X then Y then Z, scale [x, y, z]}} \
         plus, by \
         default, components: [{type, props: {field: value}}] in node order \
         (only properties stored on the node; a field left at its default is \
         absent, see get_component_schema for the defaults), and, with \
         include_children, children: [{name, uid}]. Vector2/Vector3/Color \
         values are number arrays, a NodePath is {\"path\": \"A/B\"}, a \
         Resource is {\"res\": \"res://...\"}. An unknown node returns an \
         error with a did-you-mean suggestion.",
        Mode::Read,
        || schema::<GetNodeArgs>(),
    )
}

fn read_console() -> ToolSpec {
    ToolSpec::new(
        "read_console",
        "Reads the editor's Console: editor messages, engine logs, build and \
         game output. Returns {entries: [{seq, level (\"info\", \"warning\" \
         or \"error\"), source (\"editor\", \"build\", \"game\" or \"lore\"), \
         text}], next_cursor, truncated, dropped}. Entries are oldest first. \
         To follow the console, pass the previous `next_cursor` as `cursor`: \
         you get exactly the entries logged since, once each. The console \
         holds the last 5000 lines; `dropped` is true when your cursor \
         pointed at lines already discarded (or cleared), so you missed some \
         and should read more often. Without a cursor you get the most \
         recent `limit` entries. A cursor larger than the console's latest \
         one (the editor was restarted) is an error: omit it. \
         get_editor_state reports the console's error and warning counts and \
         the current cursor.",
        Mode::Read,
        || schema::<ReadConsoleArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Value, json};

    #[test]
    fn the_group_has_five_read_tools_with_object_schemas() {
        let tools = tools();
        let names: Vec<&str> = tools.iter().map(|tool| tool.name).collect();
        assert_eq!(
            names,
            [
                "get_editor_state",
                "get_component_schema",
                "find_nodes",
                "get_node",
                "read_console"
            ]
        );
        for tool in &tools {
            assert_eq!(tool.mode, Mode::Read, "{}", tool.name);
            assert_eq!(tool.schema().get("type"), Some(&json!("object")));
            assert!(!tool.description.is_empty());
        }
    }

    // Walks a schema and collects every property that has no description.
    fn undocumented(value: &Value, path: &str, out: &mut Vec<String>) {
        if let Some(properties) = value.get("properties").and_then(Value::as_object) {
            for (name, property) in properties {
                // A property that is a $ref gets its description from the
                // referenced definition's own doc; check it has either.
                if property.get("description").is_none() && property.get("$ref").is_none() {
                    out.push(format!("{path}.{name}"));
                }
                undocumented(property, &format!("{path}.{name}"), out);
            }
        }
        if let Some(defs) = value.get("$defs").and_then(Value::as_object) {
            for (name, def) in defs {
                undocumented(def, &format!("{path}.$defs.{name}"), out);
            }
        }
    }

    #[test]
    fn every_argument_field_has_a_description() {
        for tool in tools() {
            let schema = Value::Object(tool.schema());
            let mut missing = Vec::new();
            undocumented(&schema, tool.name, &mut missing);
            assert!(missing.is_empty(), "undocumented fields: {missing:?}");
        }
    }

    fn property<'a>(schema: &'a serde_json::Map<String, Value>, name: &str) -> &'a Value {
        &schema["properties"][name]
    }

    #[test]
    fn list_limits_are_bounded_and_defaulted() {
        let find = find_nodes().schema();
        let limit = property(&find, "limit");
        assert_eq!(limit["default"], json!(100));
        assert_eq!(limit["minimum"], json!(1));
        assert_eq!(limit["maximum"], json!(1000));

        let console = read_console().schema();
        let limit = property(&console, "limit");
        assert_eq!(limit["default"], json!(200));
        assert_eq!(limit["maximum"], json!(1000));
    }

    #[test]
    fn the_raw_type_field_is_named_type_in_the_schema() {
        let schema = get_component_schema().schema();
        assert!(schema["properties"].get("type").is_some(), "{schema:?}");
        let args: GetComponentSchemaArgs =
            serde_json::from_value(json!({"type": "Rigidbody"})).unwrap();
        assert_eq!(args.r#type.as_deref(), Some("Rigidbody"));
    }

    #[test]
    fn get_node_requires_a_node_and_defaults_components_on() {
        let schema = get_node().schema();
        assert_eq!(schema["required"], json!(["node"]));
        assert_eq!(property(&schema, "include_components")["default"], json!(true));
    }

    #[test]
    fn args_deserialize_from_what_the_handlers_read() {
        let args: GetNodeArgs =
            serde_json::from_value(json!({"node": {"uid": "42"}, "include_children": true}))
                .unwrap();
        assert!(args.include_components && args.include_children);
        assert!(matches!(args.node.uid, Some(crate::tools::Uid::Text(_))));
        let args: FindNodesArgs = serde_json::from_value(json!({})).unwrap();
        assert_eq!(args.limit, 100);
        let args: ReadConsoleArgs =
            serde_json::from_value(json!({"min_level": "warning", "cursor": 7})).unwrap();
        assert_eq!(args.cursor, Some(7));
        assert!(matches!(args.min_level, Some(ConsoleLevel::Warning)));
    }
}
