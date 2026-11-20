// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * script_builder.cc
 */
#include "scripting/as/script_builder.h"

#include <algorithm>
#include <system_error>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "scripting/as/script_runtime.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

void Emit(const BuildOptions& options, std::string* log,
          const std::string& line) {
  if (log) *log += line + "\n";
  if (options.on_line) options.on_line(line);
}

}  // namespace

std::vector<fs::path> ScriptBuilder::ScriptFiles(const fs::path& project_dir) {
  std::vector<fs::path> files;
  for (const std::string& name :
       os::ListFilesRecursive(project_dir / "scripts")) {
    const fs::path relative(name);
    if (relative.extension() != ".as") continue;
    if (relative.filename().string().starts_with(".")) continue;
    files.push_back(project_dir / "scripts" / relative);
  }
  std::sort(files.begin(), files.end());
  return files;
}

fs::path ScriptBuilder::StampPath(const fs::path& project_dir) {
  std::string error;
  const fs::path cache = os::EnsureProjectCacheDir(project_dir, &error);
  if (cache.empty()) return {};
  const fs::path scripts =
      os::EnsureProjectCacheSubdir(cache, "scripts", &error);
  if (scripts.empty()) return {};
  return scripts / ".as.stamp";
}

bool ScriptBuilder::Build(const fs::path& project_dir,
                          const BuildOptions& options, std::string* log) {
  auto& runtime = ScriptRuntime::Instance();
  if (!runtime.IsInitialized() && !runtime.Initialize()) {
    Emit(options, log, "Could not start the script engine");
    return false;
  }
  const auto files = ScriptFiles(project_dir);
  if (files.empty()) {
    Emit(options, log,
         "No .as scripts in " + (project_dir / "scripts").string());
  }
  Emit(options, log,
       "Compiling " + std::to_string(files.size()) + " AngelScript file(s)");
  // A build must compile the files as they are now, not reuse the module
  // from an earlier build of the same project.
  runtime.ForgetLoadedProject();
  if (!runtime.LoadScripts(project_dir)) {
    Emit(options, log, "Script compilation failed");
    return false;
  }
  // A shipped game reads its scripts from a mounted .mpack; the project root
  // is then inside the (signed, read-only) app bundle, so leave it untouched.
  if (!os::HasMounts()) {
    const fs::path stamp = StampPath(project_dir);
    if (stamp.empty()) {
      Emit(options, log,
           "Could not use the project cache (.mercury); nothing was written");
      return false;
    }
    os::WriteFileAtomic(stamp, "1\n");
  }
  Emit(options, log, "Scripts compiled");
  return true;
}

bool ScriptBuilder::IsUpToDate(const fs::path& project_dir) {
  if (os::HasMounts()) return false;
  std::error_code ec;
  const fs::path stamp = StampPath(project_dir);
  if (stamp.empty()) return false;
  if (!fs::exists(stamp, ec)) return false;
  const auto built = fs::last_write_time(stamp, ec);
  if (ec) return false;
  for (const fs::path& file : ScriptFiles(project_dir)) {
    const auto modified = fs::last_write_time(file, ec);
    if (ec || modified > built) return false;
  }
  return true;
}

}  // namespace Mercury
