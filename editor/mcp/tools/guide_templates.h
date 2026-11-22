// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * guide_templates.h
 *
 * The MCP guide templates (the .md files in editor/mcp/guides), embedded at
 * build time: cmake/EmbedMcpGuides.cmake turns them into a generated source
 * that defines these constants, so the editor needs no file lookup at run time.
 * The templates carry {{placeholders}} that tools/guides.cc fills from the open
 * project.
 *
 * INTERNAL header, not installed in the SDK (the whole mcp/tools folder is
 * excluded).
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_GUIDE_TEMPLATES_H
#define MERCURY_EDITOR_MCP_TOOLS_GUIDE_TEMPLATES_H

#include <string_view>

namespace Mercury::mcp::guides {

// The body every guide shares (common.md).
extern const std::string_view kCommon;
// The per-agent headers (agents.md, claude.md, gemini.md).
extern const std::string_view kAgents;
extern const std::string_view kClaude;
extern const std::string_view kGemini;

}  // namespace Mercury::mcp::guides

#endif  // MERCURY_EDITOR_MCP_TOOLS_GUIDE_TEMPLATES_H
