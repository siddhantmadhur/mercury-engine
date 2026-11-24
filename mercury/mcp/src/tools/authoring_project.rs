// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The project authoring tool group: input actions, maps and project settings.
//!
//! The handlers live in `editor/mcp/tools/authoring_project.cc`. They use the
//! editor's own APIs: the .minput parser/serializer and the Input Actions
//! window's generated AngelScript header for input actions, Templates and
//! EditorScene::OpenMap for maps, and the same Project setters the editor's
//! dialogs call for settings.

use super::{Mode, ToolSpec, schema};

/// One entry of `manage_input_actions`'s `ops` array. The op names and fields
/// are read by the C++ handler; this type is the schema agents see.
#[derive(Debug, serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
#[serde(tag = "op", rename_all = "snake_case")]
pub enum InputActionOp {
    /// Create the project's first .minput from the editor's default template
    /// (a Player map with Move, Look, Jump and Fire). Only valid as the first
    /// op when the project has no input actions path yet. `path` defaults to
    /// "res://input/actions.minput"; an existing file is never overwritten.
    CreateFile {
        #[serde(default)]
        path: Option<String>,
    },
    /// Add a named action map. Duplicate maps are errors.
    AddMap { map: String },
    /// Remove a map and every action in it.
    RemoveMap { map: String },
    /// Add an action to an existing map. `type` is "button", "value" or
    /// "pass_through" (Unity's PassThrough). `vector2` marks a 2D axis.
    AddAction {
        map: String,
        name: String,
        #[serde(rename = "type")]
        action_type: String,
        #[serde(default)]
        vector2: Option<bool>,
    },
    /// Remove an action from a map.
    RemoveAction { map: String, name: String },
    /// Add a control path such as "<Keyboard>/space" to an action. Paths are
    /// checked against the Input Actions panel's known control paths.
    AddBinding {
        map: String,
        name: String,
        path: String,
    },
    /// Remove an existing binding; unknown paths are errors.
    RemoveBinding {
        map: String,
        name: String,
        path: String,
    },
    /// Add a 2D vector composite. Each of up/down/left/right is a list of
    /// control paths (any list may be empty).
    AddComposite {
        map: String,
        name: String,
        up: Vec<String>,
        down: Vec<String>,
        left: Vec<String>,
        right: Vec<String>,
    },
    /// Set the generated AngelScript path and class name. `header` must be a
    /// res:// path to an .as file. The default class is "GameInput".
    SetHeader { header: String, class_name: String },
}

/// `manage_input_actions`'s arguments (schema only; C++ reads the JSON).
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct ManageInputActionsArgs {
    /// Operations applied in order to a copy of the parsed .minput; the file
    /// is written only when every op succeeds. Omit (or pass an empty array)
    /// for a read-only description of the current file. `create_file` must be
    /// first. Unknown maps, actions, paths or op names produce errors with
    /// did-you-mean suggestions.
    #[serde(default)]
    pub ops: Option<Vec<InputActionOp>>,
}

/// `manage_maps_and_settings`'s arguments (schema only; C++ reads the JSON).
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct ManageMapsAndSettingsArgs {
    /// "create_map" (`path`, `template`, optional `open`), "open_map"
    /// (`path`, optional `discard`), "set_main_map" (`path`) or
    /// "update_settings" (`settings`).
    pub action: String,
    /// res:// path of the map: required by create_map/open_map/set_main_map.
    /// Must end in .mmap and stay inside the project.
    #[serde(default)]
    pub path: Option<String>,
    /// "2d" (root + 2D camera) or "3d" (camera, light, ground and cube) for
    /// create_map. Required there; the template is parsed before it is written.
    #[serde(default)]
    pub template: Option<String>,
    /// create_map only: open the new map after writing it. Default false.
    #[serde(default)]
    pub open: Option<bool>,
    /// open_map/create_map: allow discarding unsaved map changes. Default
    /// false; without it an open dirty map is refused with "call save_map
    /// first". Nothing is saved silently.
    #[serde(default)]
    pub discard: Option<bool>,
    /// update_settings only: dotted setting names to JSON values. Supported:
    /// "name", "render.anti_aliasing" ("none"/"fxaa"/"taa"), "render.fsr"
    /// (bool), "render.fsr_quality" ("high"/"ultra"), "render.scale"
    /// (0.5-1), "render.fsr_sharpness" (0-1) and "input_actions" (a res://
    /// path to a .minput). A nested "render" object is accepted too.
    /// [physics], [layers] and [tags] keys are not supported yet (the editor
    /// has no writing path for them); unknown keys get did-you-mean suggestions.
    #[serde(default)]
    pub settings: Option<serde_json::Map<String, serde_json::Value>>,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![manage_input_actions(), manage_maps_and_settings()]
}

fn manage_input_actions() -> ToolSpec {
    ToolSpec::new(
        "manage_input_actions",
        "Reads or edits the open project's .minput input actions file. With no \
         `ops` (or an empty array) it returns {\"path\":\"res://...\",\"maps\":[...],\
         \"actions\":[...],\"header\":\"res://...\",\"class_name\":\"GameInput\",\
         \"header_written\":false,\"header_errors\":[]}. Otherwise each op in \
         `ops` is validated against the file and applied to a copy, and the \
         file is written only if every op succeeded. Ops: create_file (only \
         when the project has no input actions yet), add_map, remove_map, \
         add_action (type \"button\", \"value\" or \"pass_through\"), \
         remove_action, add_binding, remove_binding, add_composite (up/down/\
         left/right lists of control paths) and set_header (header, class_name). \
         After a successful write the AngelScript header named in the file is \
         regenerated with GameInput::<Map>::<Action> constants; name collisions \
         are returned in \"header_errors\" and leave the header untouched. \
         The result has \"header_written\" and, when the Input Actions window \
         is open on this file, a \"warning\" to reopen that window. Requires \
         the server to allow edits.",
        Mode::Write,
        || schema::<ManageInputActionsArgs>(),
    )
}

fn manage_maps_and_settings() -> ToolSpec {
    ToolSpec::new(
        "manage_maps_and_settings",
        "Manages the open project's maps and settings. `action` is one of: \
         \"create_map\" (`path`, `template`: \"2d\" or \"3d\", optional `open`) \
         writes a new .mmap from the editor's map template and refuses to \
         overwrite an existing file; \"open_map\" (`path`, optional `discard`) \
         opens it but refuses when the current map is unsaved (save_map first, \
         or pass discard:true) and never saves silently; \"set_main_map\" \
         (`path`) changes and saves the project's start map; \"update_settings\" \
         (`settings`) changes and saves only settings with Project setters: \
         name, render.anti_aliasing (\"none\"/\"fxaa\"/\"taa\"), render.fsr \
         (bool), render.fsr_quality (\"high\"/\"ultra\"), render.scale (0.5-1), \
         render.fsr_sharpness (0-1) and input_actions (a res:// .minput path). \
         [physics], [layers] and [tags] keys are not supported yet because the \
         editor has no writing path for them; unknown keys return a \
         did-you-mean suggestion. Values are validated before anything is \
         written. Requires the server to allow edits.",
        Mode::Write,
        || schema::<ManageMapsAndSettingsArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn the_group_has_two_write_tools() {
        let tools = tools();
        let summary: Vec<(&str, Mode)> = tools.iter().map(|tool| (tool.name, tool.mode)).collect();
        assert_eq!(
            summary,
            [
                ("manage_input_actions", Mode::Write),
                ("manage_maps_and_settings", Mode::Write),
            ]
        );
    }

    #[test]
    fn manage_input_actions_args_parse_and_schema_lists_ops() {
        let args: ManageInputActionsArgs =
            serde_json::from_value(json!({})).expect("empty args parse");
        assert!(args.ops.is_none());

        let args: ManageInputActionsArgs = serde_json::from_value(json!({
            "ops": [
                {"op": "add_map", "map": "Player"},
                {"op": "add_action", "map": "Player", "name": "Jump",
                 "type": "button", "vector2": false},
                {"op": "add_binding", "map": "Player", "name": "Jump",
                 "path": "<Keyboard>/space"},
                {"op": "add_composite", "map": "Player", "name": "Move",
                 "up": ["<Keyboard>/w"], "down": ["<Keyboard>/s"],
                 "left": ["<Keyboard>/a"], "right": ["<Keyboard>/d"]},
                {"op": "set_header", "header": "res://scripts/game_input.as",
                 "class_name": "GameInput"}
            ]
        }))
        .expect("ops parse");
        let ops = args.ops.expect("ops");
        assert_eq!(ops.len(), 5);
        match &ops[1] {
            InputActionOp::AddAction {
                map,
                name,
                action_type,
                vector2,
            } => {
                assert_eq!(map, "Player");
                assert_eq!(name, "Jump");
                assert_eq!(action_type, "button");
                assert_eq!(*vector2, Some(false));
            }
            other => panic!("unexpected op: {other:?}"),
        }
        assert!(
            matches!(&ops[3], InputActionOp::AddComposite { up, .. } if up == &["<Keyboard>/w"])
        );
        assert!(
            matches!(&ops[4], InputActionOp::SetHeader { class_name, .. } if class_name == "GameInput")
        );

        let schema = schema::<ManageInputActionsArgs>();
        assert!(schema
            .get("required")
            .map_or(true, |r| r.as_array().is_some_and(Vec::is_empty)));
        let properties = schema["properties"].as_object().expect("properties");
        assert!(properties.contains_key("ops"), "property ops");
        // schemars puts the tagged enum behind a $ref; its variants are in
        // $defs, while the property only describes the array itself.
        let encoded = serde_json::to_string(&schema["$defs"]["InputActionOp"])
            .expect("encode");
        for name in [
            "create_file",
            "add_map",
            "remove_map",
            "add_action",
            "remove_action",
            "add_binding",
            "remove_binding",
            "add_composite",
            "set_header",
        ] {
            assert!(encoded.contains(name), "ops schema lists {name}: {encoded}");
        }
    }

    #[test]
    fn manage_maps_args_and_schema() {
        let create: ManageMapsAndSettingsArgs = serde_json::from_value(json!({
            "action": "create_map", "path": "res://maps/new.mmap",
            "template": "3d", "open": true,
        }))
        .expect("create args parse");
        assert_eq!(create.action, "create_map");
        assert_eq!(create.template.as_deref(), Some("3d"));
        assert_eq!(create.open, Some(true));
        assert_eq!(create.discard, None);

        let update: ManageMapsAndSettingsArgs = serde_json::from_value(json!({
            "action": "update_settings",
            "settings": {"name": "Pong", "render": {"fsr": true}},
        }))
        .expect("update args parse");
        let settings = update.settings.expect("settings");
        assert_eq!(settings["name"], json!("Pong"));
        assert_eq!(settings["render"]["fsr"], json!(true));

        let schema = schema::<ManageMapsAndSettingsArgs>();
        assert_eq!(schema["required"], json!(["action"]));
        let properties = schema["properties"].as_object().expect("properties");
        for name in ["action", "path", "template", "open", "discard", "settings"] {
            assert!(properties.contains_key(name), "property {name}");
        }
        let description = properties["settings"]["description"]
            .as_str()
            .expect("settings description");
        for name in [
            "render.anti_aliasing",
            "render.fsr_quality",
            "input_actions",
            "physics",
            "layers",
            "tags",
            "not supported yet",
        ] {
            assert!(
                description.contains(name),
                "settings description mentions {name}: {description}"
            );
        }
    }
}
