// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The play-in-editor tool group: build and run the project's map in the
//! editor's own PlaySession (the one the Play / Stop button uses), plus
//! `inspect_runtime_entity`, which runs a separate short headless player.
//!
//! The workflow is always `play_start` -> poll `play_status` and
//! `play_logs` with the returned `console_cursor` (then with each
//! `next_cursor`) until the state is `running` or `idle` -> `play_stop` when
//! finished. `play_start` only starts the build; it does not wait for the
//! game. `inspect_runtime_entity` instead replays scripted input in its own
//! player and reads back the dumps it wrote. The C++ handlers are in
//! `editor/mcp/tools/play.cc`.

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

fn default_frames() -> u32 {
    60
}

/// A scripted input line for `inspect_runtime_entity`. Only input commands
/// are allowed: `capture`, `quit`, `dump` and `log` are the tool's job.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
#[serde(rename_all = "lowercase")]
pub enum RuntimeInputCommand {
    Press,
    Release,
    Tap,
    Mouse,
    Look,
}

/// One line of the temporary input script the inspection run replays.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct RuntimeInputStep {
    /// The frame to apply the input on, from 1 to `frames`.
    #[schemars(range(min = 1, max = 3600))]
    pub frame: u32,
    /// `press`, `release`, `tap`, `mouse` or `look`. Control commands such as
    /// `capture`, `quit`, `dump` and `log` are rejected.
    pub command: RuntimeInputCommand,
    /// The command's arguments, e.g. ["w"] for press or ["10", "0"] for look.
    #[serde(default)]
    pub args: Vec<String>,
}

/// `inspect_runtime_entity`'s arguments.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct InspectRuntimeEntityArgs {
    /// A res:// path to a .mmap to run. Defaults to the map open in the
    /// editor, or the project's main map when no map is open. Only files
    /// already on disk are used.
    #[serde(default)]
    pub map: Option<String>,
    /// How many frames to simulate, from 1 to 3600. Defaults to 60.
    #[serde(default = "default_frames")]
    #[schemars(range(min = 1, max = 3600))]
    pub frames: u32,
    /// Scripted inputs to replay, e.g. {"frame": 5, "command": "press",
    /// "args": ["w"]}. Only press/release/tap/mouse/look are accepted.
    #[serde(default)]
    pub inputs: Option<Vec<RuntimeInputStep>>,
    /// Frames at which to write a runtime dump. Defaults to [frames].
    #[serde(default)]
    pub dump_at: Option<Vec<u32>>,
    /// Node paths to dump at each frame. Omit or leave empty for every node;
    /// "*" also means every node.
    #[serde(default)]
    pub nodes: Option<Vec<String>>,
    /// Save the open map before running if it has unsaved changes. Defaults
    /// to false: without it, a dirty map is an error.
    #[serde(default)]
    pub save: bool,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![
        play_start(),
        play_stop(),
        play_status(),
        play_logs(),
        inspect_runtime_entity(),
    ]
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

fn inspect_runtime_entity() -> ToolSpec {
    ToolSpec::new(
        "inspect_runtime_entity",
        "Runs the map headlessly with scripted input and reports runtime node \
         state. Refuses when a Play session is building/running, when the map \
         is missing, or when the open map is dirty unless `save` is true. \
         Replays `inputs` (only press/release/tap/mouse/look) over `frames` \
         frames (1 to 3600, default 60), writes a JSON dump at each `dump_at` \
         frame (default [frames]) for `nodes` (default every node), then \
         exits. Returns {ok, frames_run, dumps: [{frame, nodes: [...]}], \
         errors, log_tail}. The dumps contain world transforms, rigidbody \
         velocities and live component/script fields, so a caller can check \
         what a simulated player actually did. Unlike play_start this runs a \
         separate headless player and does not use play_status or play_logs.",
        Mode::Exec,
        || schema::<InspectRuntimeEntityArgs>(),
    )
    .with_timeout_secs(150)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Value, json};

    #[test]
    fn the_group_has_five_tools_with_the_right_modes_and_schemas() {
        let tools = tools();
        let names: Vec<&str> = tools.iter().map(|tool| tool.name).collect();
        assert_eq!(
            names,
            [
                "play_start",
                "play_stop",
                "play_status",
                "play_logs",
                "inspect_runtime_entity"
            ]
        );
        let modes: Vec<Mode> = tools.iter().map(|tool| tool.mode).collect();
        assert_eq!(
            modes,
            [Mode::Exec, Mode::Exec, Mode::Read, Mode::Read, Mode::Exec]
        );
        for tool in &tools {
            assert_eq!(tool.schema().get("type"), Some(&json!("object")), "{}", tool.name);
            assert!(!tool.description.is_empty());
        }
    }

    #[test]
    fn descriptions_state_the_tool_workflow() {
        for tool in tools().into_iter().filter(|tool| tool.name.starts_with("play_")) {
            let description = tool.description.to_lowercase();
            assert!(description.contains("play_start"), "{}", tool.name);
            assert!(description.contains("play_status"), "{}", tool.name);
            assert!(description.contains("play_logs"), "{}", tool.name);
            assert!(description.contains("play_stop"), "{}", tool.name);
        }
        assert!(play_start().description.contains("console_cursor"));
        assert!(play_logs().description.contains("next_cursor"));
        let inspect = inspect_runtime_entity().description.to_lowercase();
        assert!(inspect.contains("dump"));
        assert!(inspect.contains("frames"));
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

    #[test]
    fn inspect_runtime_entity_schema_is_bounded() {
        let spec = inspect_runtime_entity();
        assert_eq!(spec.mode, Mode::Exec);
        assert_eq!(spec.timeout_secs, 150);
        let schema = spec.schema();
        assert_eq!(schema["properties"]["frames"]["default"], json!(60));
        assert_eq!(schema["properties"]["frames"]["minimum"], json!(1));
        assert_eq!(schema["properties"]["frames"]["maximum"], json!(3600));
        let commands = schema["$defs"]["RuntimeInputCommand"]["enum"]
            .as_array()
            .expect("RuntimeInputCommand must be an enum in $defs");
        let commands: Vec<&str> = commands.iter().map(|c| c.as_str().unwrap()).collect();
        assert_eq!(commands, ["press", "release", "tap", "mouse", "look"]);
    }

    #[test]
    fn inspect_runtime_entity_args_default_and_validate_commands() {
        let args: InspectRuntimeEntityArgs = serde_json::from_value(json!({})).unwrap();
        assert_eq!(args.frames, 60);
        assert_eq!(args.map, None);
        assert!(args.inputs.is_none());
        assert!(args.dump_at.is_none());
        assert!(args.nodes.is_none());
        assert!(!args.save);
        let args: InspectRuntimeEntityArgs = serde_json::from_value(json!({
            "frames": 10,
            "dump_at": [5, 10],
            "nodes": ["Root/Player"],
            "inputs": [{"frame": 5, "command": "press", "args": ["w"]}]
        }))
        .unwrap();
        assert_eq!(args.frames, 10);
        assert_eq!(args.dump_at, Some(vec![5, 10]));
        assert_eq!(args.nodes, Some(vec!["Root/Player".to_string()]));
        assert_eq!(args.inputs.unwrap().len(), 1);
        for command in ["capture", "quit", "dump", "log"] {
            let bad = json!({"inputs": [{"frame": 1, "command": command}]});
            assert!(
                serde_json::from_value::<InspectRuntimeEntityArgs>(bad).is_err(),
                "{command} must be rejected"
            );
        }
    }
}
