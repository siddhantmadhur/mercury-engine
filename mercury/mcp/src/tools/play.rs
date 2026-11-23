// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The play-in-editor tool group: build and run the project's map in the
//! editor's own PlaySession (the one the Play / Stop button uses).
//!
//! The workflow is always `play_start` -> poll `play_status` and
//! `play_logs` with the returned `console_cursor` (then with each
//! `next_cursor`) until the state is `running` or `idle` -> `play_stop` when
//! finished. `play_start` only starts the build; it does not wait for the
//! game. The C++ handlers are in `editor/mcp/tools/play.cc`.

use super::{Mode, ToolSpec, schema};

const DEFAULT_LOG_LIMIT: u32 = 200;

fn default_log_limit() -> u32 {
    DEFAULT_LOG_LIMIT
}

/// `play_start`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct PlayStartArgs {
    /// A res:// path to a .mmap to play, such as "res://maps/main.mmap".
    /// Defaults to the map currently open in the editor, or the project's
    /// main map when no map is open. The file must exist on disk.
    #[serde(default)]
    pub map: Option<String>,
    /// Save the open map before playing if it has unsaved changes. Defaults
    /// to false: without it, a dirty map is an error. Only files already on
    /// disk are used by the game; unsaved script editor buffers are ignored.
    #[serde(default)]
    pub save: bool,
}

/// `play_stop`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct PlayStopArgs {}

/// `play_status`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct PlayStatusArgs {}

/// `play_logs`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct PlayLogsArgs {
    /// Where to continue reading: the `console_cursor` from `play_start`,
    /// `get_editor_state`'s `console.latest_cursor`, or the `next_cursor` of
    /// the previous `play_logs` call. Omit it to get the newest `limit`
    /// matching lines; 0 reads from the oldest line still held.
    #[serde(default)]
    pub cursor: Option<u64>,
    /// The most entries to return, 1 to 1000. Defaults to 200; values above
    /// 1000 are clamped. When more match, `truncated` is true and
    /// `next_cursor` continues after the last returned entry.
    #[serde(default = "default_log_limit")]
    #[schemars(range(min = 1, max = 1000))]
    pub limit: u32,
    /// Keep only these severities. Defaults to all of them: "info",
    /// "warning" and "error". This is an exact-level filter, not a minimum.
    #[serde(default)]
    pub levels: Option<Vec<ConsoleLevel>>,
}

/// A Console severity that `play_logs` may keep.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
#[serde(rename_all = "lowercase")]
pub enum ConsoleLevel {
    Info,
    Warning,
    Error,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![play_start(), play_stop(), play_status(), play_logs()]
}

fn play_start() -> ToolSpec {
    ToolSpec::new(
        "play_start",
        "Starts a Play session in the editor: the same build-and-run path as \
         the editor's Play button. Refuses when a session is already \
         building or running (call play_stop first), when the map does not \
         exist, or when the open map is dirty unless `save` is true. With no \
         `map`, plays the open map, or the project's main map if none is \
         open. A native (C++) project that has not been trusted in the editor \
         is refused. Only files already on disk run: unsaved script editor \
         buffers are ignored. Returns {state: \"building\" or \"idle\", map, \
         console_cursor}. WORKFLOW: call play_start, then poll play_status \
         and play_logs(cursor = console_cursor) until the game is running or \
         the build fails, then call play_stop when finished.",
        Mode::Exec,
        || schema::<PlayStartArgs>(),
    )
}

fn play_stop() -> ToolSpec {
    ToolSpec::new(
        "play_stop",
        "Stops the current Play session (the build or the game child process) \
         and returns {state: \"idle\"}. Calling it when nothing is running is \
         a successful no-op. WORKFLOW: play_start -> poll play_status and \
         play_logs(cursor) -> play_stop when finished.",
        Mode::Exec,
        || schema::<PlayStopArgs>(),
    )
}

fn play_status() -> ToolSpec {
    ToolSpec::new(
        "play_status",
        "play_status reports the Play session's state without changing it: {state: \
         \"idle\", \"building\" or \"running\", last_build_succeeded, map \
         (the map play_start was given, when known)}. \"building\" covers \
         the build and the script-schema step; \"running\" means the game \
         process has started. Poll this after play_start, alongside \
         play_logs(cursor), until the game is running or the session has \
         gone idle; poll play_logs(cursor) alongside it for the build and
         game output, then call play_stop when finished.",
        Mode::Read,
        || schema::<PlayStatusArgs>(),
    )
}

fn play_logs() -> ToolSpec {
    ToolSpec::new(
        "play_logs",
        "Reads build and game output from the editor's Console: only lines \
         whose source is \"build\" or \"game\" (editor and lore lines are \
         left out). Returns {entries: [{seq, level (\"info\", \"warning\" or \
         \"error\"), source (\"build\" or \"game\"), text}], next_cursor, \
         truncated, dropped, state}, oldest first. Pass the `console_cursor` \
         from play_start as `cursor`, then feed each `next_cursor` back to \
         see exactly the lines added since, once each. `dropped` is true if \
         the console evicted unseen lines (read more often). The default \
         `limit` is 200; `levels` keeps only the given severities. The \
         workflow is play_start -> poll play_status and play_logs(cursor) \
         -> play_stop when finished.",
        Mode::Read,
        || schema::<PlayLogsArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Value, json};

    #[test]
    fn the_group_has_four_tools_with_the_right_modes_and_schemas() {
        let tools = tools();
        let names: Vec<&str> = tools.iter().map(|tool| tool.name).collect();
        assert_eq!(names, ["play_start", "play_stop", "play_status", "play_logs"]);
        let modes: Vec<Mode> = tools.iter().map(|tool| tool.mode).collect();
        assert_eq!(modes, [Mode::Exec, Mode::Exec, Mode::Read, Mode::Read]);
        for tool in &tools {
            assert_eq!(tool.schema().get("type"), Some(&json!("object")), "{}", tool.name);
            assert!(!tool.description.is_empty());
        }
    }

    #[test]
    fn descriptions_state_the_tool_workflow() {
        for tool in tools() {
            let description = tool.description.to_lowercase();
            assert!(description.contains("play_start"), "{}", tool.name);
            assert!(description.contains("play_status"), "{}", tool.name);
            assert!(description.contains("play_logs"), "{}", tool.name);
            assert!(description.contains("play_stop"), "{}", tool.name);
        }
        assert!(play_start().description.contains("console_cursor"));
        assert!(play_logs().description.contains("next_cursor"));
    }

    #[test]
    fn every_argument_field_has_a_description() {
        for tool in tools() {
            let schema = Value::Object(tool.schema());
            if let Some(properties) = schema.get("properties").and_then(Value::as_object) {
                for (name, property) in properties {
                    assert!(
                        property.get("description").is_some(),
                        "{}.{} is undocumented",
                        tool.name,
                        name
                    );
                }
            }
        }
    }

    #[test]
    fn log_limit_is_bounded_and_defaulted() {
        let schema = play_logs().schema();
        assert_eq!(schema["properties"]["limit"]["default"], json!(200));
        assert_eq!(schema["properties"]["limit"]["minimum"], json!(1));
        assert_eq!(schema["properties"]["limit"]["maximum"], json!(1000));
    }

    #[test]
    fn args_deserialize_from_what_the_handlers_read() {
        let args: PlayStartArgs = serde_json::from_value(json!({"save": true})).unwrap();
        assert!(args.save);
        assert_eq!(args.map, None);
        let args: PlayStartArgs =
            serde_json::from_value(json!({"map": "res://maps/main.mmap"})).unwrap();
        assert_eq!(args.map.as_deref(), Some("res://maps/main.mmap"));
        assert!(!args.save);
        let args: PlayLogsArgs =
            serde_json::from_value(json!({"cursor": 7, "levels": ["warning", "error"]}))
                .unwrap();
        assert_eq!(args.cursor, Some(7));
        assert_eq!(args.limit, 200);
        assert_eq!(args.levels.unwrap().len(), 2);
    }
}
