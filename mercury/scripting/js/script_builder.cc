/*
 * script_builder.cc
 */
#include "scripting/js/script_builder.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

#include "debugger/debugger.h"
#include "os/process.h"
#include "scripting/ts_types.h"

#ifndef MERCURY_ESBUILD_PATH
#define MERCURY_ESBUILD_PATH "esbuild"
#endif

namespace Mercury {

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> ScriptFiles(const fs::path& project_dir) {
  std::vector<fs::path> files;
  const fs::path scripts = project_dir / "scripts";
  std::error_code ec;
  if (!fs::is_directory(scripts, ec)) return files;
  for (const auto& entry : fs::recursive_directory_iterator(scripts, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (entry.path().extension() != ".ts" || name.ends_with(".d.ts")) continue;
    files.push_back(fs::relative(entry.path(), project_dir, ec));
  }
  std::sort(files.begin(), files.end());
  return files;
}

// Runs a program to the end, feeding each output line to `emit`.
bool Run(const std::vector<std::string>& args, const fs::path& working_dir,
         const std::function<void(const std::string&)>& emit) {
  os::Process process;
  std::string error;
  if (!process.Start(args, working_dir, &error)) {
    emit(std::format("Could not start {}: {}", args[0], error));
    return false;
  }
  while (process.IsRunning()) {
    for (auto& line : process.TakeOutput()) emit(line);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  for (auto& line : process.TakeOutput()) emit(line);
  return process.GetExitCode().value_or(1) == 0;
}

fs::path FindTsc(const fs::path& project_dir) {
  std::error_code ec;
#if defined(_WIN32)
  const fs::path local = project_dir / "node_modules" / ".bin" / "tsc.cmd";
#else
  const fs::path local = project_dir / "node_modules" / ".bin" / "tsc";
#endif
  if (fs::exists(local, ec)) return local;
  if (os::IsOnPath("tsc")) return "tsc";
  return {};
}

}  // namespace

bool ScriptBuilder::Build(const fs::path& project_dir, const BuildOptions& options,
                          std::string* log) {
  auto emit = [&](const std::string& line) {
    if (log) *log += line + "\n";
    if (options.on_line) options.on_line(line);
  };

  std::string error;
  WriteDefaultTsconfig(project_dir, &error);
  if (!WriteTypeScriptTypes(project_dir / ".mercury" / "types", &error)) {
    emit(error);
    return false;
  }

  // The entry file only imports; the @script decorators register the classes when it runs.
  const fs::path scripts_dir = project_dir / ".mercury" / "scripts";
  std::error_code ec;
  fs::create_directories(scripts_dir, ec);
  std::string entry = "import \"mercury\";\n";
  for (const fs::path& file : ScriptFiles(project_dir)) {
    entry += "import \"../../" + file.generic_string() + "\";\n";
  }
  {
    std::ofstream out(scripts_dir / "entry.ts", std::ios::binary | std::ios::trunc);
    out << entry;
  }

  std::vector<std::string> esbuild = {
      MERCURY_ESBUILD_PATH, ".mercury/scripts/entry.ts", "--bundle", "--format=iife",
      "--platform=neutral", "--target=es2023", "--keep-names", "--tsconfig=tsconfig.json",
      "--sourcemap=external", "--outfile=.mercury/scripts/game.js", "--log-level=warning"};
  if (options.release) esbuild.push_back("--minify");
  if (!Run(esbuild, project_dir, emit)) {
    emit("esbuild failed");
    return false;
  }

  const fs::path tsc = FindTsc(project_dir);
  if (tsc.empty()) {
    static bool noted = false;
    if (!noted) {
      emit("tsc not found: skipping the type check (esbuild does not check types)");
      noted = true;
    }
  } else if (!Run({tsc.string(), "-p", project_dir.string(), "--noEmit", "--pretty", "false"},
                  project_dir, emit)) {
    emit("Type check failed");
    return false;
  }
  return true;
}

bool ScriptBuilder::IsUpToDate(const fs::path& project_dir) {
  std::error_code ec;
  const fs::path bundle = BundlePath(project_dir);
  if (!fs::exists(bundle, ec)) return false;
  const auto built = fs::last_write_time(bundle, ec);
  auto newer = [&](const fs::path& file) {
    std::error_code e;
    return fs::exists(file, e) && fs::last_write_time(file, e) > built;
  };
  if (newer(project_dir / "tsconfig.json")) return false;
  for (const fs::path& file : ScriptFiles(project_dir)) {
    if (newer(project_dir / file)) return false;
  }
  return true;
}

}  // namespace Mercury
