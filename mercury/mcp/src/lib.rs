// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! MCP streamable-HTTP transport for the Mercury editor.
//!
//! This crate is compiled only into the editor's Rust archive. It carries no
//! tool logic: tool names and JSON arguments are forwarded across the cxx
//! bridge to the C++ `ToolHost`, and the resulting JSON/images come back
//! through `CompleteReply`.

mod bridge;
