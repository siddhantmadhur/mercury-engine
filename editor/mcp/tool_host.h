// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * tool_host.h
 *
 * The interface the Mercury editor implements for the MCP transport
 * (mercury/mcp). The Rust server owns the HTTP transport only: it forwards
 * every tool call to this host and delivers the JSON result and captured
 * images back through Mercury::mcp::CompleteReply.
 *
 * Threading contract for Invoke: the MCP runtime calls it from its own worker
 * threads, never the UI thread. Implementations must be thread-safe and must
 * not touch the editor directly; queue the work onto the UI thread with a
 * CallAfter guarded by an alive token, and call CompleteReply exactly once
 * per reply. That contract is what makes sharing the host across threads
 * sound (see the unsafe impls in mercury/mcp/src/bridge.rs).
 */
#ifndef MERCURY_EDITOR_TOOL_HOST_H
#define MERCURY_EDITOR_TOOL_HOST_H

#include "rust/cxx.h"

// The reply handle is a Rust type; the generated bridge header
// (mercury/mcp/bridge.rs.h) declares it. Including that header here would be
// circular, so a forward declaration is enough for rust::Box.
namespace Mercury::mcp {
class PendingReply;
}

namespace Mercury::mcp {

class ToolHost {
 public:
  virtual ~ToolHost() = default;

  // `tool` is the editor tool name and `args_json` its JSON arguments, both
  // passed through from the MCP client unchanged. The host owns `reply` until
  // it completes it with Mercury::mcp::CompleteReply.
  virtual void Invoke(rust::Str tool, rust::Str args_json,
                      rust::Box<PendingReply> reply) const = 0;
};

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_TOOL_HOST_H
