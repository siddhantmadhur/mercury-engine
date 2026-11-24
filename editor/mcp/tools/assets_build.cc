// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The assets, validation and script build tool group: list_project_assets,
// inspect_asset, validate_project and build_scripts. The Rust side (specs and
// argument schemas) is mercury/mcp/src/tools/assets_build.rs.
//
// build_scripts drives the editor's own PlaySession (play_session.cc), the
// object the Build Scripts button and Play use, so an MCP build and a button
// build are the same build: same player binary, same trust gate, same project
// build lock, same schema refresh. This file adds only the waiting (a wxTimer
// poll instead of the frame loop alone), the capture of the build's output
// from the Console, and the parsing of that output into diagnostics.

#include "mcp/tools/assets_build.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <wx/app.h>
#include <wx/timer.h>

#include <nlohmann/json.hpp>

#include "console.h"
#include "editor_scene.h"
#include "introspection/introspection.h"
#include "mcp/tool_registry.h"
#include "play_session.h"
#include "project/asset_database.h"
#include "project/asset_importer.h"
#include "project/project.h"
#include "scripting/type_registry.h"

namespace Mercury::mcp {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kDefaultLimit = 100;
constexpr int kMaxLimit = 1000;
constexpr size_t kLogTailLines = 200;
constexpr size_t kMaxDiagnostics = 200;
constexpr size_t kMaxGlobLength = 256;
// Lines longer than this are compiler command lines, never diagnostics.
constexpr size_t kMaxParsedLineLength = 4000;
// Slightly under the 300 s the Rust spec waits, so the tool answers with its
// own message and kills the build before the transport gives up.
constexpr auto kBuildDeadline = std::chrono::seconds(290);
constexpr auto kPollInterval = std::chrono::milliseconds(100);

ToolResult Error(std::string message, std::string suggestion = {}) {
  json result = {{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = std::move(suggestion);
  return ToolResult{false, std::move(result), {}};
}

std::string DidYouMean(const std::string& suggestion) {
  return suggestion.empty()
             ? std::string()
             : std::format(" (did you mean \"{}\"?)", suggestion);
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

// ------------------------------------------------------------- Project files

struct ProjectFile {
  std::string res_path;
  fs::path file;
};

// Every regular file of the project, sorted by res:// path. Folders starting
// with "." (.mercury, .git) and folders named "build" are skipped, like the
// editor's own file lists (EditorScene::ListProjectFiles), and so are
// symlinks, so a link cannot point a listing outside the project.
std::vector<ProjectFile> WalkProject(const Project& project) {
  std::vector<ProjectFile> files;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(project.GetRoot(), ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const std::string name = it->path().filename().string();
    if (it->is_symlink(ec)) {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(ec)) {
      if (name.starts_with(".") || name == "build") {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!it->is_regular_file(ec) || name.starts_with(".")) continue;
    std::string res_path = project.ToResPath(it->path());
    if (res_path.empty()) continue;
    files.push_back({std::move(res_path), it->path()});
  }
  std::sort(files.begin(), files.end(),
            [](const ProjectFile& a, const ProjectFile& b) {
              return a.res_path < b.res_path;
            });
  return files;
}

// The asset type of a file, "" when the file is not an asset. Importer types
// come from the extension ("Model", "Texture", "Map", ...), a .masset's type
// from its header ("Material", ...), an AngelScript file is a "Script".
std::string AssetTypeOf(const ProjectFile& entry) {
  const std::string extension = Lower(entry.file.extension().string());
  if (extension == kAssetExtension) {
    Asset asset;
    if (LoadAssetFile(entry.file, asset) && !asset.type.empty()) {
      return asset.type;
    }
    return "Unknown";  // a .masset that does not parse still is an asset file
  }
  if (extension == ".as") return "Script";
  return AssetDatabase::TypeFromExtension(entry.res_path);
}

json ParseOrNull(const std::string& text) {
  return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

// ------------------------------------------------------------------- glob --

bool GlobMatchImpl(std::string_view pattern, std::string_view text) {
  while (!pattern.empty()) {
    if (pattern.front() == '*') {
      size_t stars = 0;
      while (stars < pattern.size() && pattern[stars] == '*') stars++;
      const bool crosses_folders = stars > 1;
      pattern.remove_prefix(stars);
      // "**/": the folder part may also be empty ("a/**/x" matches "a/x").
      if (crosses_folders && !pattern.empty() && pattern.front() == '/' &&
          GlobMatchImpl(pattern.substr(1), text)) {
        return true;
      }
      for (size_t i = 0;; i++) {
        if (GlobMatchImpl(pattern, text.substr(i))) return true;
        if (i >= text.size()) return false;
        if (!crosses_folders && text[i] == '/') return false;
      }
    }
    if (text.empty()) return false;
    if (pattern.front() == '?') {
      if (text.front() == '/') return false;
    } else if (pattern.front() != text.front()) {
      return false;
    }
    pattern.remove_prefix(1);
    text.remove_prefix(1);
  }
  return text.empty();
}

// ------------------------------------------------- list_project_assets tool

ToolResult ListProjectAssetsTool(EditorScene& scene, const json& args) {
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");

  std::string type_filter;
  if (args.contains("type") && !args["type"].is_null()) {
    if (!args["type"].is_string()) return Error("type must be a string");
    type_filter = args["type"].get<std::string>();
  }
  std::string glob;
  if (args.contains("path_glob") && !args["path_glob"].is_null()) {
    if (!args["path_glob"].is_string()) {
      return Error("path_glob must be a string");
    }
    glob = args["path_glob"].get<std::string>();
    if (glob.size() > kMaxGlobLength) {
      return Error(std::format("path_glob is longer than {} characters",
                               kMaxGlobLength));
    }
    if (!glob.starts_with("res://")) glob = "res://" + glob;
  }
  bool include_details = false;
  if (args.contains("include_details") && !args["include_details"].is_null()) {
    if (!args["include_details"].is_boolean()) {
      return Error("include_details must be true or false");
    }
    include_details = args["include_details"].get<bool>();
  }
  size_t limit = kDefaultLimit;
  if (args.contains("limit") && !args["limit"].is_null()) {
    if (!args["limit"].is_number_integer() ||
        args["limit"].get<int64_t>() < 1) {
      return Error(
          std::format("limit must be a whole number from 1 to {}", kMaxLimit));
    }
    limit = static_cast<size_t>(
        std::min<int64_t>(args["limit"].get<int64_t>(), kMaxLimit));
  }

  struct Match {
    const ProjectFile* file;
    std::string type;
  };
  const std::vector<ProjectFile> files = WalkProject(*project);
  std::vector<Match> assets;
  std::set<std::string> observed_types;
  for (const ProjectFile& entry : files) {
    if (!glob.empty() && !GlobMatch(glob, entry.res_path)) continue;
    std::string type = AssetTypeOf(entry);
    if (type.empty()) continue;
    observed_types.insert(type);
    assets.push_back({&entry, std::move(type)});
  }

  if (!type_filter.empty()) {
    // The types a project can hold: the importers', scripts, the registered
    // data asset types, and whatever this listing found.
    std::vector<std::string> known = {"Script"};
    for (const AssetImporterInfo& importer :
         AssetImporterRegistry::Instance().Importers()) {
      known.push_back(importer.type);
    }
    for (const AssetTypeInfo* info : TypeRegistry::Instance().AssetTypes()) {
      known.push_back(info->name);
    }
    known.insert(known.end(), observed_types.begin(), observed_types.end());
    std::sort(known.begin(), known.end());
    known.erase(std::unique(known.begin(), known.end()), known.end());

    const std::string wanted = Lower(type_filter);
    const auto exact = std::find_if(
        known.begin(), known.end(),
        [&](const std::string& name) { return Lower(name) == wanted; });
    if (exact == known.end()) {
      const auto close = introspection::Suggest(type_filter, known, 1);
      const std::string best = close.empty() ? std::string() : close.front();
      ToolResult error = Error(std::format("unknown asset type '{}'{}",
                                           type_filter, DidYouMean(best)),
                               best);
      error.result["known_types"] = known;
      return error;
    }
    assets.erase(
        std::remove_if(assets.begin(), assets.end(),
                       [&](const Match& m) { return Lower(m.type) != wanted; }),
        assets.end());
  }

  const bool truncated = assets.size() > limit;
  if (truncated) assets.resize(limit);

  json list = json::array();
  for (const Match& match : assets) {
    std::error_code ec;
    const auto size = fs::file_size(match.file->file, ec);
    json entry = {{"res_path", match.file->res_path},
                  {"type", match.type},
                  {"bytes", ec ? 0 : static_cast<uint64_t>(size)}};
    if (include_details) {
      std::string text, error;
      if (introspection::InspectAsset(*project, match.file->res_path, &text,
                                      &error)) {
        json details = ParseOrNull(text);
        entry["details"] = details.is_discarded() ? json() : std::move(details);
      } else {
        entry["details"] = {{"error", error}};
      }
    }
    list.push_back(std::move(entry));
  }
  return ToolResult{
      true, json{{"assets", std::move(list)}, {"truncated", truncated}}, {}};
}

// ---------------------------------------------------------- inspect_asset --

ToolResult InspectAssetTool(EditorScene& scene, const json& args) {
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");
  if (!args.contains("res_path") || !args["res_path"].is_string()) {
    return Error("res_path is required and must be a string");
  }
  const std::string res_path = args["res_path"].get<std::string>();

  // A path that is not there gets a did-you-mean from the project's files.
  const auto not_found = [&](const std::string& what) {
    std::vector<std::string> candidates;
    for (const ProjectFile& entry : WalkProject(*project)) {
      candidates.push_back(entry.res_path);
    }
    const auto close = introspection::Suggest(res_path, candidates, 1);
    const std::string best = close.empty() ? std::string() : close.front();
    return Error(what + DidYouMean(best), best);
  };

  std::error_code ec;
  if (!IsResPath(res_path)) {
    // "assets/player.masset" for "res://assets/player.masset" is the likely
    // slip; offer the full path when it exists.
    const fs::path with_scheme = project->Resolve("res://" + res_path);
    if (!with_scheme.empty() && fs::exists(with_scheme, ec)) {
      const std::string full = "res://" + res_path;
      return Error(
          std::format("not a res:// path: {}{}", res_path, DidYouMean(full)),
          full);
    }
    return not_found("not a res:// path: " + res_path);
  }
  const fs::path file = project->Resolve(res_path);
  if (!file.empty() && !fs::exists(file, ec)) {
    return not_found("asset not found: " + res_path);
  }

  std::string text, error;
  if (!introspection::InspectAsset(*project, res_path, &text, &error)) {
    return Error(error);
  }
  json parsed = ParseOrNull(text);
  if (parsed.is_discarded()) return Error("the asset description is not JSON");
  return ToolResult{true, std::move(parsed), {}};
}

// ------------------------------------------------------- validate_project --

ToolResult ValidateProjectTool(EditorScene& scene, const json&) {
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");
  // The live registry: the project's scripts (loaded from the last script
  // build) are known, and ValidateProject does not modify a registry that
  // already holds script entries.
  const introspection::ValidationReport report =
      introspection::ValidateProject(*project);
  json parsed = ParseOrNull(report.ToJson());
  if (parsed.is_discarded()) return Error("the validation report is not JSON");
  return ToolResult{true, std::move(parsed), {}};
}

}  // namespace

bool GlobMatch(std::string_view pattern, std::string_view path) {
  return GlobMatchImpl(pattern, path);
}

// ------------------------------------------------------------ diagnostics --

namespace {

std::string Trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    begin++;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    end--;
  }
  return text.substr(begin, end - begin);
}

std::string RelativeToRoot(std::string file, std::string_view root) {
  if (root.empty()) return file;
  std::string prefix(root);
  std::replace(prefix.begin(), prefix.end(), '\\', '/');
  if (prefix.back() != '/') prefix += '/';
  std::string generic = file;
  std::replace(generic.begin(), generic.end(), '\\', '/');
  if (generic.starts_with(prefix)) return generic.substr(prefix.size());
  return file;
}

std::string SeverityOf(const std::string& word) {
  if (word == "error" || word == "fatal error" || word == "ERROR") {
    return "error";
  }
  if (word == "warning" || word == "WARN") return "warning";
  return "note";  // note, INFO, DEBUG
}

int ToInt(const std::string& digits) {
  if (digits.empty() || digits.size() > 9) return 0;
  return std::stoi(digits);
}

}  // namespace

std::vector<BuildDiagnostic> ParseBuildDiagnostics(
    const std::vector<std::string>& lines, std::string_view project_root) {
  // PDebug's console format: "[ERROR] 2026-11-21 10:15:42 <message>".
  static const std::regex engine_line(
      R"(^\[(ERROR|WARN|INFO|DEBUG)\]\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+(.*)$)");
  // AngelScript's "<section>:<row>:<col>: <message>" (script_runtime.cc
  // MessageCallback); the section is the script's project-relative path.
  static const std::regex script_message(R"(^(.+\.as):(\d+):(\d+):\s*(.*)$)");
  static const std::regex gcc_message(
      R"(^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning|note):\s*(.*)$)");
  static const std::regex msvc_message(
      R"(^(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning|note)(?:\s+[A-Za-z]+\d+)?\s*:\s*(.*)$)");
  static const std::regex tool_message(
      R"(^([A-Za-z_+.\-]+):\s+(fatal error|error|warning):\s+(.*)$)");
  static const std::regex cmake_header(
      R"(^CMake (Error|Warning)(?: \(dev\))? at (.+?):(\d+)(?: \([^)]*\))?:\s*$)");
  static const std::regex cmake_plain(
      R"(^CMake (Error|Warning)(?: \(dev\))?:\s*(.+)$)");

  std::vector<BuildDiagnostic> found;
  const auto add = [&](std::string file, int line, int col,
                       std::string severity, std::string message) {
    if (found.size() >= kMaxDiagnostics) return;
    found.push_back({RelativeToRoot(std::move(file), project_root), line, col,
                     std::move(severity), Trim(message)});
  };

  for (size_t i = 0; i < lines.size(); i++) {
    const std::string& line = lines[i];
    if (line.empty() || line.size() > kMaxParsedLineLength) continue;
    std::smatch m;

    if (line.starts_with('[')) {
      if (std::regex_match(line, m, engine_line)) {
        const std::string tag = m[1];
        const std::string rest = m[2];
        std::smatch where;
        if (std::regex_match(rest, where, script_message)) {
          add(where[1], ToInt(where[2]), ToInt(where[3]), SeverityOf(tag),
              where[4]);
        }
      }
      continue;
    }
    if (line.starts_with("CMake ")) {
      if (std::regex_match(line, m, cmake_header)) {
        // The text sits on the following indented lines, up to a blank one.
        std::string message;
        for (size_t j = i + 1; j < lines.size() && j <= i + 4; j++) {
          const std::string text = Trim(lines[j]);
          if (text.empty()) break;
          message += (message.empty() ? "" : " ") + text;
        }
        add(m[2], ToInt(m[3]), 0, SeverityOf(Lower(m[1])), message);
      } else if (std::regex_match(line, m, cmake_plain)) {
        add("", 0, 0, SeverityOf(Lower(m[1])), m[2]);
      }
      continue;
    }
    // Cheap rejection before the lazier patterns: every remaining format has
    // one of these words.
    if (line.find("error") == std::string::npos &&
        line.find("warning") == std::string::npos &&
        line.find("note") == std::string::npos) {
      continue;
    }
    if (std::regex_match(line, m, gcc_message)) {
      add(m[1], ToInt(m[2]), ToInt(m[3]), SeverityOf(m[4]), m[5]);
    } else if (std::regex_match(line, m, msvc_message)) {
      add(m[1], ToInt(m[2]), ToInt(m[3]), SeverityOf(m[4]), m[5]);
    } else if (std::regex_match(line, m, tool_message)) {
      add("", 0, 0, SeverityOf(m[2]), m[3]);
    }
  }
  return found;
}

// ----------------------------------------------------------- build_scripts --

namespace {

// The wxTimer that drives the poll in the editor. Notify only advances the
// build: the work is in TickScriptBuild.
class BuildPollTimer : public wxTimer {
 public:
  void Notify() override { TickScriptBuild(); }
};

// The one build_scripts call that may run at a time (PlaySession itself runs
// one build at a time, and the project build lock covers C++ projects).
struct BuildJob {
  EditorScene* scene = nullptr;
  // Expires at the start of EditorScene::OnDestroy / on destruction. Held so
  // the timer and the frame-loop pump never touch a scene that is going away.
  std::weak_ptr<const void> lifetime;
  ToolDone done;
  // Console cursor taken at build start; ReadFrom() then returns every line
  // added since, even if older lines were evicted or the console was cleared.
  uint64_t console_cursor = 0;
  fs::path project_root;
  fs::path schema_path;
  std::optional<fs::file_time_type> schema_before;
  fs::file_time_type started;
  std::chrono::steady_clock::time_point deadline;
  std::unique_ptr<BuildPollTimer> timer;
};

std::unique_ptr<BuildJob>& ActiveJob() {
  static std::unique_ptr<BuildJob> job;
  return job;
}

// What the build wrote to the Console since `cursor`: the build's own lines
// (Console::Source::kBuild: the command, the child process's output) and the
// editor's lines (kEditor: PlaySession's "Build failed ..." summaries; engine
// logs also land there). Lines carry monotonic sequence numbers, so a cursor
// taken at build start still identifies the new lines if older ones were
// evicted or the console was cleared meanwhile.
std::vector<Console::Line> LinesSince(Console& console, uint64_t cursor) {
  const Console::Page page = console.ReadFrom(
      cursor, Console::Level::kInfo, std::numeric_limits<size_t>::max());
  std::vector<Console::Line> lines;
  lines.reserve(page.entries.size());
  for (const Console::Entry& entry : page.entries) {
    if (entry.line.source == Console::Source::kBuild ||
        entry.line.source == Console::Source::kEditor) {
      lines.push_back(entry.line);
    }
  }
  return lines;
}

bool SchemaRefreshed(const BuildJob& job) {
  std::error_code ec;
  const auto written = fs::last_write_time(job.schema_path, ec);
  if (ec) return false;
  return !job.schema_before || written != *job.schema_before ||
         written >= job.started;
}

// Stops the poll and releases the timer after this callback (a wxTimer must
// not be deleted from its own Notify).
void StopPolling(BuildJob& job) {
  if (!job.timer) return;
  job.timer->Stop();
  if (wxTheApp) {
    wxTheApp->CallAfter(
        [timer = std::shared_ptr<BuildPollTimer>(std::move(job.timer))] {});
  }
}

// Answers a call whose editor went away mid-build. The lifetime token expired
// at the start of OnDestroy (or with the destructor), so the job's raw scene
// pointer is stale: never touch the scene or PlayRef(). The host's tool tracker
// still gets a clean ok=false answer.
ToolResult ShutdownResult() {
  json result = {{"ok", false},
                 {"message", "editor shutting down"},
                 {"error", "editor shutting down"},
                 {"diagnostics", json::array()},
                 {"log_tail", json::array()}};
  return ToolResult{false, std::move(result), {}};
}

// Builds the reply and completes the call. `forced_message` replaces the
// summary when the tool, not the build, ended it (a timeout or shutdown).
void FinishJob(std::unique_ptr<BuildJob> job,
               const std::string& forced_message) {
  // The token goes stale at the very start of EditorScene::OnDestroy, before
  // the subsystems stop, and on destruction. If it cannot be locked the scene
  // is gone (or going): answer with the shutdown and do not dereference it.
  const std::shared_ptr<const void> token = job->lifetime.lock();
  if (!token) {
    ToolResult result = ShutdownResult();
    StopPolling(*job);
    ToolDone done = std::move(job->done);
    done(std::move(result));
    return;
  }

  EditorScene& scene = *job->scene;
  // The same step the frame loop does after a build (EditorScene::OnFrame):
  // PlaySession set the flag when it wrote .mercury/scripts.mschema, and the
  // registry reloads it. Whichever of the two gets there first does it once.
  if (scene.PlayRef().TakeSchemaUpdated()) scene.LoadScriptSchema();

  const std::vector<Console::Line> console =
      LinesSince(scene.ConsoleRef(), job->console_cursor);
  std::vector<std::string> lines;
  lines.reserve(console.size());
  for (const Console::Line& line : console) lines.push_back(line.text);

  // Report success from PlaySession's explicit build-step result, not from
  // the schema file's mtime (which can look fresh for unrelated reasons, e.g.
  // an old file with a new timestamp on a failed build). Still require the
  // script list to have been refreshed so the editor's registry is current.
  const bool build_ok = scene.PlayRef().LastBuildSucceeded();
  const bool schema_ok = SchemaRefreshed(*job);
  const bool ok = forced_message.empty() && build_ok && schema_ok;

  std::string message = forced_message;
  if (message.empty()) {
    for (auto it = console.rbegin(); it != console.rend(); ++it) {
      if (it->source == Console::Source::kEditor &&
          it->level == Console::Level::kError) {
        message = it->text;
        break;
      }
    }
  }
  if (message.empty()) {
    if (ok) {
      message = "Build succeeded; the script list was refreshed";
    } else if (!build_ok) {
      message = "The build failed";
    } else {
      message = "The build succeeded but did not refresh the script list";
    }
  } else if (ok) {
    message = "Build succeeded; the script list was refreshed";
  }

  json diagnostics = json::array();
  for (const BuildDiagnostic& d :
       ParseBuildDiagnostics(lines, job->project_root.generic_string())) {
    diagnostics.push_back({{"file", d.file},
                           {"line", d.line},
                           {"col", d.col},
                           {"severity", d.severity},
                           {"message", d.message}});
  }
  const size_t first_tail =
      lines.size() > kLogTailLines ? lines.size() - kLogTailLines : 0;
  json log_tail = json::array();
  for (size_t i = first_tail; i < lines.size(); i++)
    log_tail.push_back(lines[i]);

  json result = {{"ok", ok},
                 {"message", message},
                 {"diagnostics", std::move(diagnostics)},
                 {"log_tail", std::move(log_tail)}};
  if (!ok) result["error"] = message;
  ToolDone done = std::move(job->done);
  StopPolling(*job);
  done(ToolResult{ok, std::move(result), {}});
}

}  // namespace

std::optional<ToolResult> TryStartScriptBuild(EditorScene& scene,
                                              ToolDone done) {
  auto project = scene.GetProject();
  if (!project) return Error("no project is open");
  if (ActiveJob() || scene.PlayRef().IsBusy()) {
    return Error(
        "a script build or Play session is already running; wait for it to "
        "finish (or stop it in the editor) and call build_scripts again");
  }

  auto job = std::make_unique<BuildJob>();
  job->scene = &scene;
  job->lifetime = scene.LifetimeToken();
  job->done = std::move(done);
  job->console_cursor = scene.ConsoleRef().LatestCursor();
  job->project_root = project->GetRoot();
  job->schema_path = PlaySession::SchemaPath(*project);
  std::error_code ec;
  if (!job->schema_path.empty()) {
    const auto before = fs::last_write_time(job->schema_path, ec);
    if (!ec) job->schema_before = before;
  }
  job->started = fs::file_time_type::clock::now();
  job->deadline = std::chrono::steady_clock::now() + kBuildDeadline;

  // The editor's Build Scripts button calls exactly this
  // (editor_frame.cc): it starts the build's child process and returns.
  scene.PlayRef().Build(*project);
  if (!scene.PlayRef().IsBusy()) {
    // It refused or could not start (untrusted C++ project, no
    // mercury_player, ...): the reason is already in the Console.
    FinishJob(std::move(job), {});
    return std::nullopt;
  }

  if (wxTheApp) {
    job->timer = std::make_unique<BuildPollTimer>();
    job->timer->Start(static_cast<int>(kPollInterval.count()));
  }
  ActiveJob() = std::move(job);
  return std::nullopt;
}

namespace {

void BuildScriptsTool(EditorScene& scene, const json&, ToolDone done) {
  if (auto error = mcp::TryStartScriptBuild(scene, done)) {
    done(std::move(*error));
  }
}

}  // namespace

void TickScriptBuild() {
  std::unique_ptr<BuildJob>& slot = ActiveJob();
  if (!slot) return;
  // The editor may have been destroyed while the build was in flight: the
  // token goes stale at the very start of OnDestroy (and on destruction),
  // before the subsystems stop. Never touch the scene through the stale
  // pointer in that case -- FinishJob reports the shutdown without it.
  if (!slot->lifetime.lock()) {
    std::unique_ptr<BuildJob> job = std::move(slot);
    FinishJob(std::move(job), "editor shutting down");
    return;
  }
  EditorScene& scene = *slot->scene;
  PlaySession& play = scene.PlayRef();

  // Forward the child's output to the Console and move to the next build
  // step, as the frame loop does every frame (EditorScene::OnFrame). Calling
  // it again in the same frame is harmless: it only acts on new output or a
  // finished process.
  play.Update();
  if (play.IsBusy()) {
    if (std::chrono::steady_clock::now() < slot->deadline) return;
    play.Stop();
    std::unique_ptr<BuildJob> job = std::move(slot);
    FinishJob(std::move(job),
              std::format("The build did not finish within {} seconds and was "
                          "cancelled",
                          std::chrono::duration_cast<std::chrono::seconds>(
                              kBuildDeadline)
                              .count()));
    return;
  }
  std::unique_ptr<BuildJob> job = std::move(slot);
  FinishJob(std::move(job), {});
}

MERCURY_MCP_TOOL("list_project_assets", kRead, &ListProjectAssetsTool);
MERCURY_MCP_TOOL("inspect_asset", kRead, &InspectAssetTool);
MERCURY_MCP_TOOL("validate_project", kRead, &ValidateProjectTool);
MERCURY_MCP_TOOL_ASYNC("build_scripts", kExec, &BuildScriptsTool);

}  // namespace Mercury::mcp
