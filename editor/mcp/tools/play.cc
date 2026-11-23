// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The play-in-editor tool group: play_start, play_stop, play_status,
// play_logs and inspect_runtime_entity. play_* drive the same PlaySession as
// the editor's Play button (editor_frame.cc), so an MCP session and a button
// session share the player, trust gate, build lock and Console. The typical
// sequence is play_start, then play_status / play_logs with the returned
// console_cursor to follow the build and the game, then play_stop.
// inspect_runtime_entity instead runs a short headless player with a scripted
// input file and reads back the dumps it wrote. The Rust specs (names,
// descriptions, argument schemas) are in mercury/mcp/src/tools/play.rs.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/app.h>
#include <wx/timer.h>

#include "console.h"
#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/console_json.h"
#include "mcp/tools/play.h"
#include "os/filesystem.h"
#include "os/process.h"
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

// ------------------------------------------------------ inspect_runtime_entity
//
// Unlike play_*, which drives PlaySession, this starts its own short-lived
// mercury_player with MERCURY_INPUT_SCRIPT. The script writes runtime dumps
// (see mercury/introspection/runtime_dump.h) and quits. os::Process is the
// same child-process helper PlaySession and build_scripts use; the timer and
// scene lifetime token follow the build_scripts job so an editor shutdown or
// a stuck child cannot strand the MCP call.

constexpr auto kInspectPollInterval = std::chrono::milliseconds(100);
constexpr auto kInspectDeadline = std::chrono::seconds(120);
constexpr size_t kInspectLogTail = 50;

class InspectPollTimer : public wxTimer {
 public:
  void Notify() override { mcp::TickRuntimeInspect(); }
};

struct InputLine {
  uint32_t frame = 0;
  std::string command;
  std::vector<std::string> args;
};

struct InspectJob {
  EditorScene* scene = nullptr;
  std::weak_ptr<const void> lifetime;
  mcp::ToolDone done;
  fs::path script_path;
  std::vector<uint32_t> dump_frames;
  std::vector<fs::path> dump_paths;
  int frames_run = 0;
  std::unique_ptr<os::Process> process;
  std::vector<std::string> output;
  std::chrono::steady_clock::time_point deadline;
  std::unique_ptr<InspectPollTimer> timer;
};

std::unique_ptr<InspectJob>& ActiveInspect() {
  static std::unique_ptr<InspectJob> job;
  return job;
}

// A wxTimer must not be deleted from its own Notify (build_scripts does the
// same).
void StopInspectPolling(InspectJob& job) {
  if (!job.timer) return;
  job.timer->Stop();
  if (wxTheApp) {
    wxTheApp->CallAfter(
        [timer = std::shared_ptr<InspectPollTimer>(std::move(job.timer))] {});
  }
}

void RemoveInspectFiles(InspectJob& job) {
  std::error_code ec;
  for (const fs::path& path : job.dump_paths) fs::remove(path, ec);
  if (!job.script_path.empty()) {
    fs::remove(job.script_path, ec);
    fs::remove(job.script_path.parent_path(), ec);  // .mercury/tmp if empty
  }
}

bool ParseUint(const json& value, uint32_t* out) {
  uint64_t number = 0;
  if (value.is_number_unsigned()) {
    number = value.get<uint64_t>();
  } else if (value.is_number_integer()) {
    // nlohmann stores a literal C++ int as a signed number, while the JSON
    // parser reading a non-negative integer makes it unsigned; accept both.
    const int64_t signed_number = value.get<int64_t>();
    if (signed_number < 0) return false;
    number = static_cast<uint64_t>(signed_number);
  } else {
    return false;
  }
  if (number > std::numeric_limits<uint32_t>::max()) return false;
  *out = static_cast<uint32_t>(number);
  return true;
}

bool ParseInputLines(const json& args, uint32_t frames,
                     std::vector<InputLine>* out, std::string* error) {
  out->clear();
  if (!args.contains("inputs") || args.at("inputs").is_null()) return true;
  const json& inputs = args.at("inputs");
  if (!inputs.is_array()) {
    *error = "\"inputs\" must be an array of input steps";
    return false;
  }
  static const char* kAllowed[] = {"press", "release", "tap", "mouse", "look"};
  for (const json& value : inputs) {
    if (!value.is_object()) {
      *error = "\"inputs\" must contain only objects";
      return false;
    }
    InputLine line;
    if (!value.contains("frame") ||
        !ParseUint(value.at("frame"), &line.frame) || line.frame < 1 ||
        line.frame > frames) {
      *error = "each input \"frame\" must be an integer from 1 to " +
               std::to_string(frames);
      return false;
    }
    if (!value.contains("command") || !value.at("command").is_string()) {
      *error = "each input needs a string \"command\"";
      return false;
    }
    line.command = value.at("command").get<std::string>();
    if (std::find(std::begin(kAllowed), std::end(kAllowed), line.command) ==
        std::end(kAllowed)) {
      *error = "input command \"" + line.command +
               "\" is not allowed; use press, release, tap, mouse or look";
      return false;
    }
    if (!value.contains("args")) {
      // No arguments is valid for this schema, but the player would reject
      // the line; report it here instead.
      *error = "input \"" + line.command + "\" needs an \"args\" array";
      return false;
    }
    const json& step_args = value.at("args");
    if (!step_args.is_array()) {
      *error = "input \"args\" must be an array of strings";
      return false;
    }
    for (const json& argument : step_args) {
      if (!argument.is_string()) {
        *error = "input \"args\" must contain only strings";
        return false;
      }
      line.args.push_back(argument.get<std::string>());
    }
    const size_t expected = line.command == "press" ||
                                    line.command == "release" ||
                                    line.command == "tap"
                                ? 1u
                                : 2u;
    if (line.args.size() != expected) {
      *error = "input \"" + line.command + "\" needs " +
               std::to_string(expected) + " argument(s)";
      return false;
    }
    out->push_back(std::move(line));
  }
  return true;
}

// Builds the answer once the child has exited. The dump files are read here
// (not by TickRuntimeInspect) so the whole output is available, then removed.
void FinishInspectJob(std::unique_ptr<InspectJob> job,
                      const std::string& forced_error) {
  const bool alive = !job->lifetime.expired();
  json errors = json::array();
  json dumps = json::array();
  if (!alive) {
    errors.push_back("editor shutting down");
  } else {
    for (size_t i = 0; i < job->dump_paths.size(); i++) {
      const auto text = os::ReadText(job->dump_paths[i]);
      if (!text) {
        errors.push_back("could not read dump at frame " +
                         std::to_string(job->dump_frames[i]));
        continue;
      }
      try {
        const json document = json::parse(*text);
        dumps.push_back({{"frame", job->dump_frames[i]},
                         {"nodes", document.value("nodes", json::array())}});
        for (const json& problem : document.value("errors", json::array())) {
          errors.push_back(problem.get<std::string>());
        }
      } catch (const json::exception&) {
        errors.push_back("invalid dump at frame " +
                         std::to_string(job->dump_frames[i]));
      }
    }
  }
  const int exit_code = job->process && job->process->GetExitCode()
                            ? *job->process->GetExitCode()
                            : 1;
  for (const std::string& line : job->output) {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    if (lower.find("error") != std::string::npos) errors.push_back(line);
  }
  json log_tail = json::array();
  const size_t first = job->output.size() > kInspectLogTail
                           ? job->output.size() - kInspectLogTail
                           : 0;
  for (size_t i = first; i < job->output.size(); i++)
    log_tail.push_back(job->output[i]);

  // A run is only a success if it exited normally AND every requested
  // dump_at frame produced a readable dump. Missing or invalid dumps are
  // still listed in "errors".
  const bool all_dumps_read = dumps.size() == job->dump_frames.size();
  const bool ok =
      alive && forced_error.empty() && exit_code == 0 && all_dumps_read;
  json result = {{"ok", ok},
                 {"frames_run", ok ? job->frames_run : 0},
                 {"dumps", std::move(dumps)},
                 {"errors", std::move(errors)},
                 {"log_tail", std::move(log_tail)}};
  if (!ok) {
    if (!forced_error.empty()) {
      result["error"] = forced_error;
    } else if (!all_dumps_read) {
      result["error"] =
          "the run finished but not every requested dump could be read; see "
          "errors";
    } else {
      result["error"] = "the headless player did not finish successfully";
    }
  }
  StopInspectPolling(*job);
  RemoveInspectFiles(*job);
  mcp::ToolDone done = std::move(job->done);
  done(ToolResult{ok, std::move(result), {}});
}

void InspectRuntimeEntityTool(EditorScene& scene, const json& args,
                              mcp::ToolDone done) {
  auto project = scene.GetProject();
  if (!project) {
    done(Fail("no project is open"));
    return;
  }
  if (ActiveInspect() || scene.PlayRef().IsBusy()) {
    done(
        Fail("a Play session or runtime inspection is already running; call "
             "play_stop first or wait for it to finish"));
    return;
  }

  std::string error;
  std::optional<std::string> map_arg;
  bool save = false;
  if (!OptionalString(args, "map", &map_arg, &error) ||
      !OptionalBool(args, "save", false, &save, &error)) {
    done(Fail(error));
    return;
  }
  uint32_t frames = 60;
  if (args.contains("frames") && !args.at("frames").is_null() &&
      !ParseUint(args.at("frames"), &frames)) {
    done(Fail("\"frames\" must be an integer from 1 to 3600"));
    return;
  }
  if (frames < 1 || frames > 3600) {
    done(Fail("\"frames\" must be between 1 and 3600"));
    return;
  }
  std::vector<uint32_t> dump_frames;
  if (args.contains("dump_at") && !args.at("dump_at").is_null()) {
    if (!args.at("dump_at").is_array()) {
      done(Fail("\"dump_at\" must be an array of frame numbers"));
      return;
    }
    for (const json& value : args.at("dump_at")) {
      uint32_t frame = 0;
      if (!ParseUint(value, &frame) || frame < 1 || frame > frames) {
        done(Fail("\"dump_at\" frames must be between 1 and " +
                  std::to_string(frames)));
        return;
      }
      dump_frames.push_back(frame);
    }
  } else {
    dump_frames.push_back(frames);
  }
  std::sort(dump_frames.begin(), dump_frames.end());
  dump_frames.erase(std::unique(dump_frames.begin(), dump_frames.end()),
                    dump_frames.end());

  std::vector<InputLine> input_lines;
  if (!ParseInputLines(args, frames, &input_lines, &error)) {
    done(Fail(error));
    return;
  }
  std::stable_sort(
      input_lines.begin(), input_lines.end(),
      [](const InputLine& a, const InputLine& b) { return a.frame < b.frame; });

  std::vector<std::string> nodes;
  if (args.contains("nodes") && !args.at("nodes").is_null()) {
    if (!args.at("nodes").is_array()) {
      done(Fail("\"nodes\" must be an array of node paths"));
      return;
    }
    for (const json& value : args.at("nodes")) {
      if (!value.is_string()) {
        done(Fail("\"nodes\" must contain only node paths"));
        return;
      }
      const std::string path = value.get<std::string>();
      // The input script separates paths with ';' and keeps their spaces.
      if (path.find(';') != std::string::npos ||
          path.find('\n') != std::string::npos) {
        done(Fail(
            "a \"nodes\" entry must not contain ';' or a newline: the input "
            "script joins paths with ';'"));
        return;
      }
      if (path == "*") {
        nodes.clear();  // wildcard: dump every node
        break;
      }
      nodes.push_back(path);
    }
  }

  const std::string map = map_arg ? *map_arg : DefaultMap(scene, *project);
  if (map.empty()) {
    done(Fail("no map is open and the project has no main map; pass \"map\""));
    return;
  }
  if (!MapExists(*project, map)) {
    done(Fail(std::format(
        "map not found: {} (give a res:// path to an existing map)", map)));
    return;
  }
  // The same explicit-save policy as play_start: an agent must not write its
  // open map unless it asked for it.
  if (scene.IsDirty()) {
    if (!save) {
      done(
          Fail("the open map has unsaved changes; call save_map or pass save: "
               "true"));
      return;
    }
    if (!scene.SaveMap()) {
      done(Fail("could not save the open map"));
      return;
    }
  }

  const fs::path player = PlaySession::PlayerPath();
  if (player.empty()) {
    done(
        Fail("mercury_player was not found next to the editor; build the "
             "mercury_player target to use inspect_runtime_entity"));
    return;
  }
  std::string cache_error;
  const fs::path cache = project->EnsureCacheDir(&cache_error);
  if (cache.empty()) {
    done(Fail("Project cache: " + cache_error));
    return;
  }
  const fs::path temp_dir = cache / "tmp";
  std::error_code ec;
  if (fs::is_symlink(temp_dir, ec)) {
    done(
        Fail("Project cache tmp is a symlink; refusing to write an input "
             "script there"));
    return;
  }
  fs::create_directories(temp_dir, ec);
  if (ec) {
    done(Fail("Project cache tmp: " + ec.message()));
    return;
  }

  // The job reads the dumps back by absolute path. The script gets a relative
  // path instead: its file argument is the first whitespace token, so an
  // absolute path under a directory with spaces would be split into nodes.
  // Application::WritePendingDump resolves it against the project root.
  std::vector<fs::path> dump_paths;
  for (uint32_t frame : dump_frames) {
    dump_paths.push_back(temp_dir /
                         ("dump_" + std::to_string(frame) + ".json"));
  }

  const std::string stamp = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path script_path = temp_dir / ("inspect_" + stamp + ".txt");
  std::string script;
  const auto append_line = [&script](const std::string& line) {
    script += line + "\n";
  };
  for (const InputLine& line : input_lines) {
    std::string text = std::format("{} {}", line.frame, line.command);
    for (const std::string& argument : line.args) text += " " + argument;
    append_line(text);
  }
  for (size_t i = 0; i < dump_frames.size(); i++) {
    std::string text = std::format("{} dump .mercury/tmp/dump_{}.json",
                                   dump_frames[i], dump_frames[i]);
    for (size_t n = 0; n < nodes.size(); n++) {
      text += (n == 0 ? " " : ";");
      text += nodes[n];
    }
    append_line(text);
  }
  append_line(std::format("{} quit", frames));
  if (!os::WriteFileAtomic(script_path, script)) {
    done(Fail("could not write the input script to the project cache"));
    return;
  }

  auto process = std::make_unique<os::Process>();
  const std::vector<std::string> player_args = {
      player.string(), project->GetFile().string(), "--map", map, "--headless",
      "--frames",      std::to_string(frames + 5)};
  if (!process->Start(player_args, project->GetRoot(), &error,
                      {"MERCURY_INPUT_SCRIPT=" + script_path.string()})) {
    done(Fail("could not start mercury_player: " + error));
    fs::remove(script_path, ec);
    fs::remove(temp_dir, ec);
    return;
  }

  auto job = std::make_unique<InspectJob>();
  job->scene = &scene;
  job->lifetime = scene.LifetimeToken();
  job->done = std::move(done);
  job->script_path = script_path;
  job->frames_run = static_cast<int>(frames);
  job->process = std::move(process);
  job->deadline = std::chrono::steady_clock::now() + kInspectDeadline;
  job->dump_paths = std::move(dump_paths);
  job->dump_frames = std::move(dump_frames);
  if (wxTheApp) {
    job->timer = std::make_unique<InspectPollTimer>();
    job->timer->Start(static_cast<int>(kInspectPollInterval.count()));
  }
  ActiveInspect() = std::move(job);
}

}  // namespace

namespace mcp {

void TickRuntimeInspect() {
  std::unique_ptr<InspectJob>& slot = ActiveInspect();
  if (!slot) return;
  // The token expires at the start of EditorScene::OnDestroy, before its
  // subsystems stop. Never touch the scene through the stale pointer then.
  if (slot->lifetime.expired()) {
    std::unique_ptr<InspectJob> job = std::move(slot);
    FinishInspectJob(std::move(job), "editor shutting down");
    return;
  }
  const auto collect_output = [&] {
    std::vector<std::string> lines = slot->process->TakeOutput();
    slot->output.insert(slot->output.end(),
                        std::make_move_iterator(lines.begin()),
                        std::make_move_iterator(lines.end()));
  };
  collect_output();
  if (slot->process->IsRunning()) {
    if (std::chrono::steady_clock::now() < slot->deadline) return;
    slot->process->Kill();
    std::unique_ptr<InspectJob> job = std::move(slot);
    FinishInspectJob(std::move(job),
                     "The headless player did not finish within 120 seconds "
                     "and was cancelled");
    return;
  }
  // The reader thread can still have the child's last lines buffered when
  // IsRunning() first observes the exit.
  collect_output();
  std::unique_ptr<InspectJob> job = std::move(slot);
  FinishInspectJob(std::move(job), {});
}

}  // namespace mcp

MERCURY_MCP_TOOL("play_start", kExec, &PlayStart);
MERCURY_MCP_TOOL("play_stop", kExec, &PlayStop);
MERCURY_MCP_TOOL("play_status", kRead, &PlayStatus);
MERCURY_MCP_TOOL("play_logs", kRead, &PlayLogs);
MERCURY_MCP_TOOL_ASYNC("inspect_runtime_entity", kExec,
                       &InspectRuntimeEntityTool);

}  // namespace Mercury
