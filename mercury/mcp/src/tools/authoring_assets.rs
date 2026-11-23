// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The authoring tool group: scaffold scripts and write data assets.
//!
//! Both tools are implemented in the editor, in
//! `editor/mcp/tools/authoring_assets.cc`:
//!
//! - `scaffold_script` calls `EditorScene::CreateScript`, the same creation
//!   path the New Script dialog uses. With `"build": true` it starts the same
//!   build `build_scripts` runs and waits for it, so its answer also carries
//!   `{"ok":bool,"diagnostics":[...]}` under `build`.
//! - `create_or_update_asset` loads a `.masset` through the editor's asset
//!   database or starts a new one, checks its type and fields against the live
//!   type registry, and writes it with `SaveAssetFile`.

use super::{Mode, ToolSpec, schema};

/// `scaffold_script`'s arguments (schema only; the C++ handler reads the JSON).
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct ScaffoldScriptArgs {
    /// The class name, e.g. "PlayerController". Must be a valid identifier:
    /// letters, digits and underscores, starting with a letter, no C++
    /// keywords and no Mercury base type names. Invalid names return the
    /// editor validator's reason. The snake_case file name is derived from it.
    pub name: String,
    /// What to create: "angel_script" (an .as Behaviour in scripts/),
    /// "cpp_behaviour" (a C++ .h/.cc Behaviour in src/), "cpp_system" (a
    /// scene-wide C++ System in src/) or "cpp_editor_system" (an editor module
    /// in system/). These are the only accepted values; any other kind is an
    /// error. `fields` is not supported by the templates.
    pub kind: String,
    /// When true, build the project after creating the files with the same
    /// build `build_scripts` runs, and wait for it (up to five minutes). The
    /// result then has a `build` object with `ok` and `diagnostics`, and the
    /// call is a failure when the build fails. Default false.
    #[serde(default)]
    pub build: Option<bool>,
}

/// `create_or_update_asset`'s arguments (schema only).
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct CreateOrUpdateAssetArgs {
    /// The asset's project path, for example "res://assets/enemy.masset". It
    /// must start with "res://", end in ".masset" and stay inside the project.
    /// A path that does not exist creates a new asset; an existing one is
    /// updated in place.
    pub path: String,
    /// The registered data asset type, e.g. "Material" or "PhysicsMaterial".
    /// Required when creating; on an update it must match the file's type.
    /// An unknown type is an error with a did-you-mean suggestion.
    #[serde(default, rename = "type")]
    pub asset_type: Option<String>,
    /// Field name to JSON value, e.g. {"roughness": 0.25, "unlit": true}.
    /// Names and values are checked against the type's reflected fields; an
    /// unknown name returns a did-you-mean suggestion. Only the listed fields
    /// are written, other fields keep their current or default value.
    pub fields: serde_json::Map<String, serde_json::Value>,
    /// When true, refuse to touch an asset that already exists instead of
    /// updating it. Default false.
    #[serde(default)]
    pub create_only: Option<bool>,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![scaffold_script(), create_or_update_asset()]
}

fn scaffold_script() -> ToolSpec {
    ToolSpec::new(
        "scaffold_script",
        "Creates a new script from the editor's own template: the class name is \
         validated, the .as/.h/.cc files are generated in the project's scripts/, \
         src/ or system/ folder, and existing files are never overwritten. The \
         kind must be one of \"angel_script\", \"cpp_behaviour\", \"cpp_system\" \
         or \"cpp_editor_system\". Returns {\"created\":[\"res://...\"],\
         \"class_name\":\"...\",\"kind\":\"...\"}. With \"build\": true it then \
         runs the same build as `build_scripts` (up to five minutes), adds \
         \"ok\" and {\"ok\":bool,\"diagnostics\":[...]} under \"build\", and \
         fails the call when the build fails. Script templates do not take \
         fields; set field values in the script file after creation. Requires \
         the server to allow edits.",
        Mode::Write,
        || schema::<ScaffoldScriptArgs>(),
    )
    .with_timeout_secs(300)
}

fn create_or_update_asset() -> ToolSpec {
    ToolSpec::new(
        "create_or_update_asset",
        "Creates or updates a .masset data asset in the open project. Give a \
         \"res://...\" path ending in .masset, a registered asset `type` when \
         creating (for example \"Material\" or \"PhysicsMaterial\"), and a \
         `fields` object of field names to values. Existing assets are loaded \
         through the editor's asset database and only the given fields are \
         changed; new assets start from the type's defaults. Every type and \
         field name is checked against the live type registry, with a \
         did-you-mean suggestion for a bad name, and every value is checked \
         against its field type (the same conversion `apply_scene_operations` \
         uses). Paths outside the project and non-.masset paths are rejected; \
         `create_only` refuses to overwrite an existing file. Returns \
         {\"path\":\"res://...\",\"type\":\"...\",\"created\":bool,\
         \"fields_written\":[\"...\"]}. Use `inspect_asset` to read the result. \
         Requires the server to allow edits.",
        Mode::Write,
        || schema::<CreateOrUpdateAssetArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn the_group_has_two_write_tools() {
        let tools = tools();
        let summary: Vec<(&str, Mode, u64)> = tools
            .iter()
            .map(|tool| (tool.name, tool.mode, tool.timeout_secs))
            .collect();
        assert_eq!(
            summary,
            [
                ("scaffold_script", Mode::Write, 300),
                (
                    "create_or_update_asset",
                    Mode::Write,
                    crate::tools::DEFAULT_TIMEOUT_SECS
                ),
            ]
        );
    }

    #[test]
    fn scaffold_script_args_parse_with_defaults() {
        let args: ScaffoldScriptArgs = serde_json::from_value(json!({
            "name": "PlayerController",
            "kind": "cpp_behaviour",
        }))
        .expect("minimal args parse");
        assert_eq!(args.name, "PlayerController");
        assert_eq!(args.kind, "cpp_behaviour");
        assert_eq!(args.build, None);

        let args: ScaffoldScriptArgs = serde_json::from_value(json!({
            "name": "Player", "kind": "angel_script", "build": true,
        }))
        .expect("build arg parses");
        assert_eq!(args.build, Some(true));
    }

    #[test]
    fn scaffold_script_schema_lists_the_kinds() {
        let schema = schema::<ScaffoldScriptArgs>();
        assert_eq!(schema["required"], json!(["name", "kind"]));
        let properties = schema["properties"].as_object().expect("properties");
        let kind = properties.get("kind").expect("kind");
        let description = kind["description"].as_str().expect("description");
        for name in [
            "angel_script",
            "cpp_behaviour",
            "cpp_system",
            "cpp_editor_system",
        ] {
            assert!(
                description.contains(name),
                "kind description lists {name}: {description}"
            );
        }
    }

    #[test]
    fn create_or_update_asset_args_and_schema() {
        let args: CreateOrUpdateAssetArgs = serde_json::from_value(json!({
            "path": "res://assets/enemy.masset",
            "type": "Material",
            "fields": {"roughness": 0.25, "unlit": true},
            "create_only": true,
        }))
        .expect("args parse");
        assert_eq!(args.asset_type.as_deref(), Some("Material"));
        assert_eq!(args.fields["roughness"], json!(0.25));
        assert_eq!(args.create_only, Some(true));

        let schema = schema::<CreateOrUpdateAssetArgs>();
        assert_eq!(schema["required"], json!(["path", "fields"]));
        let properties = schema["properties"].as_object().expect("properties");
        for name in ["path", "type", "fields", "create_only"] {
            assert!(properties.contains_key(name), "property {name}");
        }
    }
}
