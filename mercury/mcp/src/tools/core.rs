// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The core tool group: the handshake tools every client sees first.
//!
//! `ping`'s implementation arrives with the editor tool handlers (P2); it
//! returns `{"pong":true,"editor_version":"<version>"}`. `get_server_info` is
//! answered entirely in Rust by the transport.

use super::{Mode, ToolSpec, schema};

/// The version reported by `get_server_info` and `initialize`.
pub(crate) const SERVER_VERSION: &str = env!("CARGO_PKG_VERSION");

/// `ping`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct PingArgs {}

/// `get_server_info`'s arguments: none.
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct GetServerInfoArgs {}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![ping(), get_server_info()]
}

fn ping() -> ToolSpec {
    ToolSpec {
        name: "ping",
        description: "Checks that the Mercury editor is reachable. Returns \
                      {\"pong\":true,\"editor_version\":\"<version>\"}.",
        mode: Mode::Read,
        input_schema: || schema::<PingArgs>(),
    }
}

fn get_server_info() -> ToolSpec {
    ToolSpec {
        name: "get_server_info",
        description: "Returns the MCP server's version, URL, read-only flag \
                      and tool count.",
        mode: Mode::Read,
        input_schema: || schema::<GetServerInfoArgs>(),
    }
}
