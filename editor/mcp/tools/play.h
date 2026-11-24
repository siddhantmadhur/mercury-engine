// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * play.h
 *
 * The parts of the play tool group that callers outside play.cc need.
 * INTERNAL header: editor/mcp/tools is excluded from the SDK install.
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_PLAY_H
#define MERCURY_EDITOR_MCP_TOOLS_PLAY_H

namespace Mercury::mcp {

/**
 * @brief Advances the running inspect_runtime_entity call, if there is one
 *
 * The tool arms a wxTimer that calls this every 100 ms in the editor. A
 * process without a wxApp (the tool tests) has no timer; its pump calls this
 * instead. Does nothing when no inspection is pending. On the UI thread.
 */
void TickRuntimeInspect();

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOLS_PLAY_H
