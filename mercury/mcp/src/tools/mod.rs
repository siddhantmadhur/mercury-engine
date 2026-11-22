// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The MCP tool catalogue.
//!
//! The catalogue lives in Rust so that `tools/list` is answered without an
//! editor round-trip; the tool implementations stay in C++. Tools are grouped
//! by feature: each group module exports one `tools() -> Vec<ToolSpec>`, and
//! [`all`] concatenates them. A later work package adds its tools by adding
//! one `mod` line here plus one concat line in [`all`].
//!
//! # Per-tool-group convention
//!
//! Every group follows the same layout, so packages can add tools in parallel
//! without touching each other's files:
//!
//! - The Rust spec (name, description, mode, args schema) lives in
//!   `mercury/mcp/src/tools/<group>.rs`, plus one `pub mod <group>;` line and
//!   one concat line in [`all`] in this file.
//! - The C++ handlers live in `editor/mcp/tools/<group>.cc`, registered with
//!   `MERCURY_MCP_TOOL`. That folder is globbed by CMake and linked into the
//!   editor and every editor-linked test, so a new file needs no CMake edit.
//! - The tests live in `tests/mcp_tools_<group>_test.cc`: CMake globs
//!   `tests/mcp_tools_*_test.cc` into the `mcp_tools_test` target, so a new
//!   file needs no CMake edit. `tests/mcp_tools_fixture.h` opens a copy of a
//!   real project and calls tools through the registry.
//! - A tool that cannot answer in one UI-thread callback (it waits for a
//!   rendered frame, a child process, ...) registers with
//!   `MERCURY_MCP_TOOL_ASYNC` and its spec sets a longer timeout with
//!   [`ToolSpec::with_timeout_secs`]; the default is
//!   [`DEFAULT_TIMEOUT_SECS`].
//! - Tool names are snake_case and identical on both sides. The args schema
//!   comes from a schemars struct whose fields match what the C++ handler
//!   reads from its `args`.
//! - Errors are returned as `ok = false` with `{"error": "...", "suggestion"?:
//!   "..."}`.
//!
//! `core.rs` / `editor/mcp/tools/core.cc` are the worked example.

use serde_json::{Map, Value};

pub mod assets_build;
pub mod core;
pub mod scene_read;
pub mod scene_write;
pub mod spatial_capture;
pub mod guides;

// Argument types shared by the tool groups: every tool that takes a node
// reference uses `NodeSelector` so they all advertise the same schema.

/// A uid as either a JSON number or a decimal string.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
#[serde(untagged)]
pub enum Uid {
    Number(u64),
    Text(String),
}

/// A reference to one node of the open map: give exactly one of `uid` and
/// `path`.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[allow(dead_code)]
pub struct NodeSelector {
    /// The node's uid, from any tool result (`uid`). Stable while the editor
    /// is running and the node exists; not saved in the map file. A number or
    /// a decimal string.
    #[serde(default)]
    pub uid: Option<Uid>,
    /// The node's full path starting at the root's name, such as
    /// "Root/Player/Camera". The engine's own form without the root
    /// ("Player/Camera", and "." for the root) is accepted too.
    #[serde(default)]
    pub path: Option<String>,
}

/// How long a tool call may wait on the editor before the transport answers
/// "editor did not respond", unless the tool's spec says otherwise. Tool calls
/// queue onto the editor's UI thread, so this has to ride out a busy frame
/// loop.
pub const DEFAULT_TIMEOUT_SECS: u64 = 30;

/// What a tool may do to the editor, used for the read-only gate.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
// Write appears with the first write tools, Exec with the exec tools; the
// enum is defined up front so the read-only gate never changes.
#[allow(dead_code)]
pub enum Mode {
    /// Pure queries; always allowed.
    Read,
    /// Mutates editor or project state; rejected when the server is read-only.
    Write,
    /// Runs user code or processes; rejected when the server is read-only.
    Exec,
}

/// One entry of the tool catalogue: everything `tools/list` needs.
#[derive(Clone)]
pub struct ToolSpec {
    pub name: &'static str,
    pub description: &'static str,
    pub mode: Mode,
    /// The JSON Schema of the tool's arguments, generated from a schemars
    /// type so it always matches what the C++ handler expects.
    pub input_schema: fn() -> Map<String, Value>,
    /// How long the transport waits for the editor's reply to this tool.
    pub timeout_secs: u64,
}

impl ToolSpec {
    /// A spec with the default timeout ([`DEFAULT_TIMEOUT_SECS`]).
    pub fn new(
        name: &'static str,
        description: &'static str,
        mode: Mode,
        input_schema: fn() -> Map<String, Value>,
    ) -> Self {
        ToolSpec {
            name,
            description,
            mode,
            input_schema,
            timeout_secs: DEFAULT_TIMEOUT_SECS,
        }
    }

    /// The same spec with its own reply timeout, for tools that legitimately
    /// take long (a build, a capture).
    #[allow(dead_code)] // used by the tool groups that need it
    pub fn with_timeout_secs(mut self, timeout_secs: u64) -> Self {
        self.timeout_secs = timeout_secs;
        self
    }

    /// Whether the read-only gate lets this tool through.
    pub fn is_read_only(&self) -> bool {
        self.mode == Mode::Read
    }

    /// The tool's input schema.
    pub fn schema(&self) -> Map<String, Value> {
        (self.input_schema)()
    }
}

/// The whole catalogue: every group's tools, concatenated.
pub fn all() -> Vec<ToolSpec> {
    let mut tools = core::tools();
    tools.extend(scene_read::tools());
    tools.extend(scene_write::tools());
    tools.extend(assets_build::tools());
    tools.extend(spatial_capture::tools());
    tools.extend(guides::tools());
    tools
}

/// The JSON Schema of a schemars type, as the `inputSchema` object rmcp wants.
pub(crate) fn schema<T: schemars::JsonSchema>() -> Map<String, Value> {
    match schemars::schema_for!(T).to_value() {
        Value::Object(map) => map,
        _ => unreachable!("a root JSON Schema is always an object"),
    }
}
