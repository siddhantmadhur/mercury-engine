// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The guides tool group: the agent guides served as MCP resources and
//! through `get_agent_guides`.
//!
//! The handler is in `editor/mcp/tools/guides.cc`; the resources
//! (`mercury://guides/*`, `mercury://project/*`, `mercury://asset/{+path}`)
//! are answered by `editor/mcp/tools/resources.cc` through the transport's
//! reserved `__resources/list` and `__resources/read` names, so they have no
//! entry in this catalogue.

use super::{Mode, ToolSpec, schema};

/// `get_agent_guides`' arguments. Only the schema is used on this side; the C++
/// handler reads the arguments.
#[allow(dead_code)]
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct GetAgentGuidesArgs {
    /// Which guides to return: any of "AGENTS.md", "CLAUDE.md" and
    /// "GEMINI.md". Leave it out (or empty) to get all three.
    #[serde(default)]
    pub files: Option<Vec<String>>,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![get_agent_guides()]
}

fn get_agent_guides() -> ToolSpec {
    ToolSpec::new(
        "get_agent_guides",
        "Returns the Mercury project guides for AI agents (AGENTS.md, \
         CLAUDE.md, GEMINI.md), generated from the open project: what Mercury \
         is, the file formats, the coordinate and unit conventions (Y up, \
         Euler degrees, world units), the AngelScript script lifecycle, the \
         recommended workflow (get_editor_state, find_nodes/get_node, \
         apply_scene_operations, capture_viewport, validate_project, \
         save_map), the tool catalogue with each tool's Read/Write/Exec mode, \
         and the project's name, main map, layers, tags, input actions and \
         sample assets. Read this first when you do not know the engine's \
         conventions. The three files share one body and differ in the \
         connection instructions at the top (Claude Code and Gemini CLI \
         setup); use `files` to fetch only the one you need. It does not \
         write any file: copy the text into the project yourself if the user \
         wants it there. The guides never contain the bearer token; they say \
         where to find it. Returns {\"files\": [{\"name\": \"AGENTS.md\", \
         \"content\": \"<markdown>\"}, ...]}. The same texts are the \
         resources mercury://guides/AGENTS.md, mercury://guides/CLAUDE.md and \
         mercury://guides/GEMINI.md.",
        Mode::Read,
        || schema::<GetAgentGuidesArgs>(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn get_agent_guides_is_a_read_tool_with_documented_args() {
        let specs = tools();
        assert_eq!(specs.len(), 1);
        let spec = &specs[0];
        assert_eq!(spec.name, "get_agent_guides");
        assert!(spec.is_read_only());

        let schema = spec.schema();
        let files = &schema["properties"]["files"];
        assert!(
            files["description"].as_str().is_some_and(|d| d.contains("AGENTS.md")),
            "the files argument documents the accepted names: {files}"
        );
        // Optional: nothing is required.
        assert!(schema.get("required").map_or(true, |r| r.as_array().is_some_and(Vec::is_empty)));
    }
}
