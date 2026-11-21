// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/tool_registry.h"

#include <atomic>
#include <cassert>
#include <utility>

#include "editor_scene.h"
#include "editor_settings.h"

namespace Mercury::mcp {

ToolRegistry& ToolRegistry::Instance() {
  static ToolRegistry registry;
  return registry;
}

namespace {

std::atomic<bool>& AssertOnDoubleDone() {
  static std::atomic<bool> enabled{true};
  return enabled;
}

ToolResult ErrorResult(const std::string& message) {
  return ToolResult{false, nlohmann::json{{"error", message}}, {}};
}

}  // namespace

void ToolRegistry::Register(std::string name, ToolMode mode,
                            ToolHandler handler) {
  // Build the entry first, then key the map on a copy of the name: moving
  // the same string into both would leave one of them empty.
  ToolEntry entry{std::move(name), mode, std::move(handler), {}};
  tools_.insert_or_assign(entry.name, std::move(entry));
}

void ToolRegistry::RegisterAsync(std::string name, ToolMode mode,
                                 AsyncToolHandler handler) {
  ToolEntry entry{std::move(name), mode, {}, std::move(handler)};
  tools_.insert_or_assign(entry.name, std::move(entry));
}

const ToolEntry* ToolRegistry::Find(const std::string& name) const {
  const auto it = tools_.find(name);
  return it == tools_.end() ? nullptr : &it->second;
}

std::optional<ToolResult> ToolRegistry::Reject(const EditorScene& scene,
                                               const ToolEntry* entry,
                                               const std::string& name) const {
  if (entry == nullptr) return ErrorResult("unknown tool " + name);
  // Defence in depth: the Rust transport already got the mode as its
  // read_only flag, but the settings are checked again here, on the UI
  // thread, before any handler runs.
  if (entry->mode != ToolMode::kRead && !scene.Settings().mcp_allow_edits) {
    return ErrorResult("tool '" + name +
                       "' changes the editor and edits are not allowed");
  }
  return std::nullopt;
}

ToolResult ToolRegistry::Invoke(EditorScene& scene, const std::string& name,
                                const nlohmann::json& args) const {
  const ToolEntry* entry = Find(name);
  if (auto rejected = Reject(scene, entry, name)) return *rejected;
  if (entry->IsAsync()) {
    return ErrorResult("tool '" + name +
                       "' is asynchronous; call it through InvokeAsync");
  }
  return entry->handler(scene, args);
}

void ToolRegistry::InvokeAsync(EditorScene& scene, const std::string& name,
                               const nlohmann::json& args,
                               ToolDone done) const {
  const ToolEntry* entry = Find(name);
  if (auto rejected = Reject(scene, entry, name)) {
    done(std::move(*rejected));
    return;
  }
  if (entry->IsAsync()) {
    entry->async_handler(scene, args, std::move(done));
    return;
  }
  done(entry->handler(scene, args));
}

// ------------------------------------------------------------ CallTracker ---

void CallTracker::SetAssertOnDoubleDone(bool enable) {
  AssertOnDoubleDone() = enable;
}

ToolDone CallTracker::Track(std::function<void(ToolResult)> finish) {
  auto call = std::make_shared<Call>();
  call->finish = std::move(finish);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Forget completed calls so the list stays as long as the pending ones.
    std::erase_if(calls_, [](const std::shared_ptr<Call>& tracked) {
      std::lock_guard<std::mutex> call_lock(tracked->mutex);
      return tracked->completed;
    });
    calls_.push_back(call);
  }
  // The returned function holds its own reference: it only ever touches the
  // call, never the tracker or the host, so it stays safe after both are gone.
  return [call](ToolResult result) {
    std::function<void(ToolResult)> finish;
    {
      std::lock_guard<std::mutex> lock(call->mutex);
      if (call->completed) {
        // After a shutdown answer a late `done` is expected; otherwise the
        // tool called it twice.
        assert((call->failed_by_shutdown || !AssertOnDoubleDone()) &&
               "an async MCP tool called done() twice");
        return;
      }
      call->completed = true;
      finish = std::move(call->finish);
    }
    finish(std::move(result));
  };
}

size_t CallTracker::FailAll(const std::string& message) {
  std::vector<std::shared_ptr<Call>> calls;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    calls.swap(calls_);
  }
  size_t failed = 0;
  for (const std::shared_ptr<Call>& call : calls) {
    std::function<void(ToolResult)> finish;
    {
      std::lock_guard<std::mutex> lock(call->mutex);
      if (call->completed) continue;
      call->completed = true;
      call->failed_by_shutdown = true;
      finish = std::move(call->finish);
    }
    finish(ErrorResult(message));
    failed++;
  }
  return failed;
}

size_t CallTracker::Pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t pending = 0;
  for (const std::shared_ptr<Call>& call : calls_) {
    std::lock_guard<std::mutex> call_lock(call->mutex);
    if (!call->completed) ++pending;
  }
  return pending;
}

}  // namespace Mercury::mcp
