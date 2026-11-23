// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The play-in-editor tool group: play_start, play_stop, play_status and
// play_logs. These drive the same PlaySession as the editor's Play button
// (editor_frame.cc), so an MCP session and a button session share the player,
// trust gate, build lock and Console. The typical sequence is play_start,
// then play_status / play_logs with the returned console_cursor to follow the
// build and the game, then play_stop. The Rust specs (names, descriptions,
// argument schemas) are in mercury/mcp/src/tools/play.rs.

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/console_json.h"
#include "play_session.h"
#include "project/project.h"

namespace Mercury {

namespace fs = std::filesystem;
namespace {

using mcp::ToolResult;
using nlohmann::json;

constexpr int kDefaultLogLimit = 200;
constexpr int kMaxLogLimit = 1000;

ToolResult Ok(json result) { return ToolResult{true, std::move(result), {}}; }

ToolResult Fail(const std::string& error) {
  return ToolResult{false, json{{"error", error}}, {}};
}

bool OptionalString(const json& args, const char* key,
                    std::optional<std::string>* out, std::string* error) {
  out->reset();
  if (!args.contains(key) || args.at(key).is_null()) return true;
  if (!args.at(key).is_string()) {
    *error = std::format("\"{}\" must be a string", key);
    return false;
  }
  *out = args.at(key).get<std::string>();
  return true;
}

bool OptionalBool(const json& args, const char* key, bool fallback, bool* out,
                  std::string* error) {
  *out = fallback;
  if (!args.contains(key) || args.at(key).is_null()) return true;
  if (!args.at(key).is_boolean()) {
    *error = std::format("\"{}\" must be true or false", key);
    return false;
  }
  *out = args.at(key).get<bool>();
  return true;
}

const char* StateName(PlaySession::State state) {
  switch (state) {
    case PlaySession::State::kIdle:
      return "idle";
    case PlaySession::State::kBuilding:
      return "building";
    case PlaySession::State::kRunning:
      return "running";
  }
  return "idle";
}

// PlaySession keeps the map it was started with private. Remember it here for
// play_status, tied to the scene's lifetime token so a new EditorScene in the
// same process cannot inherit a stale map from a previous session.
struct LastStart {
  std::weak_ptr<const void> lifetime;
  const EditorScene* scene = nullptr;
  std::string map;
};

LastStart& LastStarted() {
  static LastStart last;
  return last;
}

void RememberMap(EditorScene& scene, const std::string& map) {
  LastStart& last = LastStarted();
  last.lifetime = scene.LifetimeToken();
  last.scene = &scene;
  last.map = map;
}

std::optional<std::string> KnownMap(const EditorScene& scene) {
  const LastStart& last = LastStarted();
  if (last.scene == &scene && !last.lifetime.expired() && !last.map.empty()) {
    return last.map;
  }
  return std::nullopt;
}

// The map play_start would use when the caller does not give one: the open
// map, or the project's main map when no map is open.
std::string DefaultMap(EditorScene& scene, const Project& project) {
  if (!scene.GetMapPath().empty()) return scene.GetMapPath();
  return project.GetMainMap();
}

bool MapExists(const Project& project, const std::string& map) {
  if (!IsResPath(map)) return false;
  std::error_code ec;
  const fs::path file = project.Resolve(map);
  return !file.empty() && fs::exists(file, ec) && fs::is_regular_file(file, ec);
}

// ---------------------------------------------------------------- play_start

ToolResult PlayStart(EditorScene& scene, const json& args) {
  auto project = scene.GetProject();
  if (!project) return Fail("no project is open");

  PlaySession& play = scene.PlayRef();
  if (play.IsBusy()) {
    return Fail("already building/running; call play_stop first");
  }

  std::string error;
  std::optional<std::string> map_arg;
  bool save = false;
  if (!OptionalString(args, "map", &map_arg, &error) ||
      !OptionalBool(args, "save", false, &save, &error)) {
    return Fail(error);
  }

  const std::string map = map_arg ? *map_arg : DefaultMap(scene, *project);
  if (map.empty()) {
    return Fail("no map is open and the project has no main map; pass \"map\"");
  }
  if (!MapExists(*project, map)) {
    return Fail(std::format(
        "map not found: {} (give a res:// path to an existing map)", map));
  }

  // The editor's Play button saves the open map first (editor_frame.cc). An
  // agent has to ask for that explicitly: unlike the button, it cannot be
  // surprised by a write to its map.
  if (scene.IsDirty()) {
    if (!save) {
      return Fail(
          "the open map has unsaved changes; call save_map or pass "
          "save: true");
    }
    if (!scene.SaveMap()) return Fail("could not save the open map");
  }

  // There is no UI to confirm the prompt here, and PlaySession::Begin fails
  // closed for an untrusted C++ project. Report the reason as a tool error
  // rather than returning a "running" state that never starts.
  if (!project->GetBuildTarget().empty() &&
      !scene.Settings().IsProjectTrusted(project->GetRoot())) {
    return Fail(
        "project is not trusted to run native code; open the editor "
        "and confirm the trust prompt");
  }

  const uint64_t console_cursor = scene.ConsoleRef().LatestCursor();
  play.Play(*project, map);
  if (!play.IsBusy()) {
    ToolResult result =
        Fail("play could not start; see the console for the reason");
    result.result["console_cursor"] = console_cursor;
    return result;
  }
  RememberMap(scene, map);
  return Ok({{"state", StateName(play.GetState())},
             {"map", map},
             {"console_cursor", console_cursor}});
}

// ----------------------------------------------------------------- play_stop

ToolResult PlayStop(EditorScene& scene, const json&) {
  scene.PlayRef().Stop();
  return Ok({{"state", StateName(scene.PlayRef().GetState())}});
}

// ---------------------------------------------------------------- play_status

ToolResult PlayStatus(EditorScene& scene, const json&) {
  const PlaySession& play = scene.PlayRef();
  json result = {{"state", StateName(play.GetState())},
                 {"last_build_succeeded", play.LastBuildSucceeded()}};
  if (auto map = KnownMap(scene)) result["map"] = *map;
  return Ok(std::move(result));
}

// ----------------------------------------------------------------- play_logs

bool ParseLevels(const json& args,
                 std::optional<std::vector<Console::Level>>* out,
                 std::string* error) {
  out->reset();
  if (!args.contains("levels") || args.at("levels").is_null()) return true;
  const json& levels = args.at("levels");
  if (!levels.is_array()) {
    *error =
        "\"levels\" must be an array of \"info\", \"warning\" and/or "
        "\"error\"";
    return false;
  }
  std::vector<Console::Level> parsed;
  for (const json& value : levels) {
    if (!value.is_string()) {
      *error =
          "\"levels\" must contain only \"info\", \"warning\" and/or "
          "\"error\"";
      return false;
    }
    const std::string name = value.get<std::string>();
    if (name == "info") {
      parsed.push_back(Console::Level::kInfo);
    } else if (name == "warning") {
      parsed.push_back(Console::Level::kWarning);
    } else if (name == "error") {
      parsed.push_back(Console::Level::kError);
    } else {
      *error = std::format(
          "\"levels\" contains an unknown level \"{}\"; use \"info\", "
          "\"warning\" or \"error\"",
          name);
      return false;
    }
  }
  *out = std::move(parsed);
  return true;
}

bool Wanted(const Console::Entry& entry,
            const std::optional<std::vector<Console::Level>>& levels) {
  if (entry.line.source != Console::Source::kBuild &&
      entry.line.source != Console::Source::kGame) {
    return false;
  }
  return !levels || std::find(levels->begin(), levels->end(),
                              entry.line.level) != levels->end();
}

ToolResult PlayLogs(EditorScene& scene, const json& args) {
  std::string error;
  size_t limit = 0;
  std::optional<uint64_t> cursor;
  std::optional<std::vector<Console::Level>> levels;
  if (!mcp::ParseConsoleLimit(args, kDefaultLogLimit, kMaxLogLimit, &limit,
                              &error) ||
      !mcp::ParseConsoleCursor(args, scene.ConsoleRef(), &cursor, &error) ||
      !ParseLevels(args, &levels, &error)) {
    return Fail(error);
  }

  // Read every level the console holds and filter here: the source filter is
  // MCP-side, and limit/truncated/next_cursor must count only the lines this
  // tool returns, like read_console does with its level filter.
  Console& console = scene.ConsoleRef();
  const Console::Page raw = console.ReadFrom(
      cursor, Console::Level::kInfo, std::numeric_limits<size_t>::max());
  std::vector<Console::Entry> matches;
  for (const Console::Entry& entry : raw.entries) {
    if (Wanted(entry, levels)) matches.push_back(entry);
  }

  Console::Page page;
  page.dropped = raw.dropped;
  page.next_cursor = raw.next_cursor;
  if (cursor) {
    if (matches.size() > limit) {
      matches.resize(limit);
      page.truncated = true;
    }
    if (!matches.empty()) page.next_cursor = matches.back().seq + 1;
    page.entries = std::move(matches);
  } else {
    const size_t first = matches.size() > limit ? matches.size() - limit : 0;
    page.truncated = first > 0;
    page.entries.assign(matches.begin() + first, matches.end());
  }

  json result = mcp::ConsolePageToJson(page);
  result["state"] = StateName(scene.PlayRef().GetState());
  return Ok(std::move(result));
}

}  // namespace

MERCURY_MCP_TOOL("play_start", kExec, &PlayStart);
MERCURY_MCP_TOOL("play_stop", kExec, &PlayStop);
MERCURY_MCP_TOOL("play_status", kRead, &PlayStatus);
MERCURY_MCP_TOOL("play_logs", kRead, &PlayLogs);

}  // namespace Mercury
