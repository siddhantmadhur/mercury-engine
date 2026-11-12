/*
 * script_builder.cc
 */
#include "scripting/as/script_builder.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "debugger/debugger.h"
#include "scripting/as/script_runtime.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

void Emit(const BuildOptions& options, std::string* log, const std::string& line) {
  if (log) *log += line + "\n";
  if (options.on_line) options.on_line(line);
}

}  // namespace

std::vector<fs::path> ScriptBuilder::ScriptFiles(const fs::path& project_dir) {
  std::vector<fs::path> files;
  std::error_code ec;
  const fs::path scripts = project_dir / "scripts";
  if (!fs::is_directory(scripts, ec)) return files;
  for (const auto& entry : fs::recursive_directory_iterator(scripts, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    if (entry.path().extension() != ".as") continue;
    const std::string name = entry.path().filename().string();
    if (name.starts_with(".")) continue;
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

fs::path ScriptBuilder::StampPath(const fs::path& project_dir) {
  return project_dir / ".mercury" / "scripts" / ".as.stamp";
}

bool ScriptBuilder::Build(const fs::path& project_dir, const BuildOptions& options,
                          std::string* log) {
  auto& runtime = ScriptRuntime::Instance();
  if (!runtime.IsInitialized() && !runtime.Initialize()) {
    Emit(options, log, "Could not start the script engine");
    return false;
  }
  const auto files = ScriptFiles(project_dir);
  if (files.empty()) {
    Emit(options, log, "No .as scripts in " + (project_dir / "scripts").string());
  }
  Emit(options, log, "Compiling " + std::to_string(files.size()) + " AngelScript file(s)");
  if (!runtime.LoadScripts(project_dir)) {
    Emit(options, log, "Script compilation failed");
    return false;
  }
  std::error_code ec;
  fs::create_directories(StampPath(project_dir).parent_path(), ec);
  std::ofstream stamp(StampPath(project_dir), std::ios::trunc);
  stamp << "1\n";
  Emit(options, log, "Scripts compiled");
  return true;
}

bool ScriptBuilder::IsUpToDate(const fs::path& project_dir) {
  std::error_code ec;
  const fs::path stamp = StampPath(project_dir);
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
