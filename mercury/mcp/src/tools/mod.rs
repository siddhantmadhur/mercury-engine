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

use serde_json::{Map, Value};

pub mod core;

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
}

impl ToolSpec {
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
    core::tools()
}

/// The JSON Schema of a schemars type, as the `inputSchema` object rmcp wants.
pub(crate) fn schema<T: schemars::JsonSchema>() -> Map<String, Value> {
    match schemars::schema_for!(T).to_value() {
        Value::Object(map) => map,
        _ => unreachable!("a root JSON Schema is always an object"),
    }
}
