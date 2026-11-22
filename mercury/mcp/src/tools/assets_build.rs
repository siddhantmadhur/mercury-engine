// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The assets, validation and script build tool group.
//!
//! All four tools are implemented in the editor, in
//! `editor/mcp/tools/assets_build.cc`:
//!
//! - `list_project_assets` and `inspect_asset` read the open project's files
//!   through the engine's own asset code;
//! - `validate_project` runs the engine's project validator against the live
//!   type registry;
//! - `build_scripts` runs the same build the editor's Build and Play buttons
//!   run (an asynchronous tool: it answers when the build finishes).

use super::{Mode, ToolSpec, schema};

/// `list_project_assets`' arguments. The fields exist for the schema: the
/// editor reads the JSON arguments itself.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct ListProjectAssetsArgs {
    /// Only list assets of this type, matched case-insensitively. The types
    /// are the engine's asset types: "Map", "Prefab", "Model", "Texture",
    /// "AudioClip", "Font", "UXML", "StyleSheet", "AnimatorController",
    /// "InputActions", "Script" (an AngelScript .as file), plus the type
    /// written in a .masset file's header ("Material", "PhysicsMaterial", ...).
    /// An unknown type is an error with a suggestion.
    #[serde(default)]
    pub r#type: Option<String>,
    /// Only list assets whose res:// path matches this glob. "*" matches any
    /// characters except "/", "**" matches any characters including "/", and
    /// "?" matches one character other than "/". The "res://" prefix is
    /// optional: "assets/**/*.png" and "res://assets/**/*.png" are the same.
    #[serde(default)]
    pub path_glob: Option<String>,
    /// When true, each asset also carries `details`, the same JSON that
    /// `inspect_asset` returns for it (model bounds and clips, texture size,
    /// audio duration, .masset fields). Slower for big projects: leave it off
    /// to list, then call `inspect_asset` for the few assets you care about.
    /// Default false.
    #[serde(default)]
    pub include_details: Option<bool>,
    /// The most assets to return. Default 100; values above 1000 are clamped
    /// to 1000; values below 1 or non-integers are errors. When more match,
    /// `truncated` is true: narrow the filters or raise the limit.
    #[serde(default)]
    #[schemars(range(min = 1))]
    pub limit: Option<u32>,
}

/// `inspect_asset`'s arguments (schema only, like the others).
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct InspectAssetArgs {
    /// The asset's project path, for example "res://assets/player.masset" or
    /// "res://models/ship.glb". Use `list_project_assets` to find paths. A
    /// path that does not exist is an error with a did-you-mean suggestion.
    pub res_path: String,
}

/// `validate_project`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct ValidateProjectArgs {}

/// `build_scripts`' arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct BuildScriptsArgs {}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![
        list_project_assets(),
        inspect_asset(),
        validate_project(),
        build_scripts(),
    ]
}

fn list_project_assets() -> ToolSpec {
    ToolSpec::new(
        "list_project_assets",
        "Lists the open project's asset files (maps, prefabs, models, textures, audio, \
         fonts, UI files, .masset data assets and AngelScript scripts) sorted by res:// \
         path, optionally filtered by `type` and a `path_glob`. Use it to find the \
         res:// path of an asset before referencing it in a component field or calling \
         `inspect_asset`. Returns {\"assets\":[{\"res_path\":\"res://...\",\"type\":\"...\",\
         \"bytes\":<file size in bytes>,\"details\"?:{...}}],\"truncated\":bool}; `details` \
         (the `inspect_asset` JSON) is present only when include_details is true. \
         `limit` defaults to 100 (values above 1000 are clamped to 1000); `truncated` is true when more assets matched. \
         Folders starting with \".\" (such as .mercury) and \"build\" folders are skipped.",
        Mode::Read,
        || schema::<ListProjectAssetsArgs>(),
    )
}

fn inspect_asset() -> ToolSpec {
    ToolSpec::new(
        "inspect_asset",
        "Describes what is inside one project asset file. Models: bounds (min and max \
         corner, as imported), node hierarchy with local transforms and animation \
         clips (name, duration in seconds). Textures: width and height in pixels, pixel \
         format, sRGB flag. Audio clips: duration in seconds, channels, sample rate. \
         .masset data assets: the header type, the property values the file sets and, \
         for registered types, every field with its type, default and whether the file \
         sets it. Other files: their type and size in bytes. Use it before assigning an \
         asset to a field, to check a model's size or which clips it has. An unknown \
         res:// path is an error with a did-you-mean suggestion taken from the project's \
         files.",
        Mode::Read,
        || schema::<InspectAssetArgs>(),
    )
}

fn validate_project() -> ToolSpec {
    ToolSpec::new(
        "validate_project",
        "Checks every project file (.mproj, .mmap, .mprefab, .masset, .manim, .minput) \
         with the engine's own loaders, against the editor's live type registry (so the \
         project's built scripts are known). Reports parse errors, unknown field names \
         and enum values, unknown component or asset type names, and res:// references \
         to files that do not exist. Run it after editing project files, and after \
         `build_scripts` if a script's fields changed. Returns {\"has_errors\":bool,\
         \"diagnostics\":[{\"file\":\"res://...\" or \"\",\"line\":<1-based, 0 when \
         unknown>,\"severity\":\"error\"|\"warning\",\"message\":\"...\",\
         \"suggestion\":\"did-you-mean value or empty\"}]}. A clean project has \
         has_errors false. Warnings (an unknown component or asset type name) may be a \
         script that has not been built yet.",
        Mode::Read,
        || schema::<ValidateProjectArgs>(),
    )
}

fn build_scripts() -> ToolSpec {
    ToolSpec::new(
        "build_scripts",
        "Builds the project's scripts exactly like the editor's Build Scripts button and \
         Play do, then refreshes the script list the Inspector and `validate_project` \
         use. An AngelScript project is compiled with mercury_player --build-scripts; a \
         project with a C++ build target (`[build] target` in the .mproj) is configured \
         and built with CMake, which runs project code, so the project must already be \
         trusted in the editor (a refusal says so). Waits for the build to finish (up to \
         five minutes; a cold C++ build can be slow) without freezing the editor. Fails \
         when another Build or Play is already running. Requires the server to allow \
         edits (it runs processes and writes the project's .mercury cache). Returns \
         {\"ok\":bool,\"message\":\"short summary\",\"diagnostics\":[{\"file\":\"...\",\
         \"line\":<1-based, 0 when unknown>,\"col\":<1-based, 0 when unknown>,\
         \"severity\":\"error\"|\"warning\",\"message\":\"...\"}],\"log_tail\":[\"last \
         200 lines of build output\"]}. Diagnostics are parsed from: AngelScript \
         messages (\"[ERROR] <date> <time> scripts/x.as:12:5: message\", file relative \
         to the project), GCC/Clang compiler messages (\"file:line:col: error: \
         message\"), MSVC messages (\"file(line,col): error C1234: message\") and CMake \
         messages (\"CMake Error at CMakeLists.txt:12 (command):\" followed by its text). \
         Lines it cannot place still appear in log_tail.",
        Mode::Exec,
        || schema::<BuildScriptsArgs>(),
    )
    .with_timeout_secs(300)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_group_has_four_tools_with_the_right_modes() {
        let tools = tools();
        let summary: Vec<(&str, Mode, u64)> = tools
            .iter()
            .map(|tool| (tool.name, tool.mode, tool.timeout_secs))
            .collect();
        assert_eq!(
            summary,
            [
                (
                    "list_project_assets",
                    Mode::Read,
                    crate::tools::DEFAULT_TIMEOUT_SECS
                ),
                (
                    "inspect_asset",
                    Mode::Read,
                    crate::tools::DEFAULT_TIMEOUT_SECS
                ),
                (
                    "validate_project",
                    Mode::Read,
                    crate::tools::DEFAULT_TIMEOUT_SECS
                ),
                ("build_scripts", Mode::Exec, 300),
            ]
        );
    }

    #[test]
    fn list_project_assets_schema_names_its_arguments() {
        let schema = schema::<ListProjectAssetsArgs>();
        let properties = schema["properties"].as_object().expect("properties");
        for name in ["type", "path_glob", "include_details", "limit"] {
            let property = properties.get(name).unwrap_or_else(|| panic!("no {name}"));
            assert!(
                property["description"]
                    .as_str()
                    .is_some_and(|text| !text.is_empty()),
                "{name} has a description"
            );
        }
    }

    #[test]
    fn inspect_asset_requires_a_path() {
        let schema = schema::<InspectAssetArgs>();
        assert_eq!(schema["required"], serde_json::json!(["res_path"]));
    }
}
