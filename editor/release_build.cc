// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "release_build.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

#include "cmake_build.h"
#include "os/filesystem.h"
#include "pack/mpack.h"
#include "play_session.h"
#include "project_scaffold.h"
#include "scripting/as/script_builder.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

// Source code and editor files stay out of game.mpack.
bool IsSourceFile(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const char* skip :
       {".cc", ".cpp", ".cxx", ".c", ".h", ".hh", ".hpp", ".tmp", ".bzl"}) {
    if (ext == skip) return true;
  }
  return false;
}

bool IsSkippedAtRoot(const std::string& name) {
  return name == "CMakeLists.txt" || name == "CMakePresets.json" ||
         name == "CMakeCache.txt" || name == "CMakeFiles" ||
         name == "Cargo.toml" || name == "Cargo.lock" || name == "build" ||
         name == "target" || name == "compile_commands.json" ||
         name.starts_with("cmake-build") || name == "Builds";
}

std::string FormatBytes(uintmax_t bytes) {
  if (bytes < 1024) return std::format("{} B", bytes);
  if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
  if (bytes < 1024ull * 1024 * 1024) {
    return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
  }
  return std::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

// Total size of <dir>/<base>.mpack and its _NNN archives, and how many there
// are.
std::pair<uintmax_t, int> ArchiveSizes(const fs::path& dir,
                                       std::string_view base) {
  uintmax_t total = 0;
  int count = 0;
  std::error_code ec;
  for (;; count++) {
    const auto size = fs::file_size(mpack::ArchivePath(dir, base, count), ec);
    if (ec) break;
    total += size;
  }
  return {total, count};
}

std::string Trim(std::string text) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
             text.end());
  return text;
}

// The runtime libraries and the MoltenVK manifest that the editor bundle
// carries for its player. A game folder written by Build Game needs its own
// copies: the player it ships is the same binary, with @rpath install names.
bool CopyBundledRuntime(const ReleaseBuild::Runtime& runtime,
                        const fs::path& output, std::vector<std::string>& log,
                        std::string& error) {
  std::error_code ec;
  if (!runtime.library_list.empty()) {
    std::ifstream list(runtime.library_list);
    if (list) {
      int copied = 0;
      for (std::string line; std::getline(list, line);) {
        const std::string name = Trim(line);
        if (name.empty() || name.starts_with('#')) continue;
        const fs::path source = fs::path(runtime.frameworks_dir) / name;
        if (!fs::exists(source, ec)) {
          error = "the editor is missing its bundled runtime library " + name +
                  " (" + source.string() + ")";
          return false;
        }
        fs::copy_file(source, output / name,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
          error = "could not copy " + source.string() + ": " + ec.message();
          return false;
        }
        copied++;
      }
      if (copied > 0) {
        log.push_back(std::format(
            "Copied {} runtime libraries next to the game", copied));
      }
    }
  }
  if (!runtime.icd_manifest.empty() && fs::exists(runtime.icd_manifest, ec)) {
    const fs::path dir = output / "vulkan" / "icd.d";
    fs::create_directories(dir, ec);
    if (ec) {
      error = "could not create " + dir.string() + ": " + ec.message();
      return false;
    }
    fs::copy_file(runtime.icd_manifest, dir / "MoltenVK_icd.json",
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
      error = "could not copy the Vulkan driver manifest: " + ec.message();
      return false;
    }
    log.push_back("Copied the bundled MoltenVK driver manifest");
  }
  return true;
}

}  // namespace

ReleaseBuild::~ReleaseBuild() {
  if (packing_.valid()) packing_.wait();
}

std::string ReleaseBuild::DefaultOutput(const Project& project) {
  return "Builds/" + project.GetName();
}

void ReleaseBuild::Start(const Project& project, const fs::path& output_dir,
                         uint64_t max_archive_bytes) {
  if (IsBusy()) return;
  max_archive_bytes_ = max_archive_bytes;
  project_root_ = project.GetRoot();
  project_name_ = project.GetName();
  build_target_ = project.GetBuildTarget();
  build_dir_.clear();
  configuring_ = false;
  if (!build_target_.empty()) {
    // A native build runs the project's CMake code; fail closed unless the
    // workspace was trusted (the UI prompts before calling this).
    if (!settings_.IsProjectTrusted(project_root_)) {
      Finish(
          "This project is not trusted, so its C++ build was not run. Use "
          "Project > Trust Editor Systems and accept the prompt first.",
          true);
      return;
    }
    if (cmake_build::SdkCmakeDir().empty()) {
      Finish(
          "This project names a C++ build target, but the editor's SDK has no "
          "CMake package. A development checkout must be configured once; a "
          "packaged editor should include Contents/Resources/sdk.",
          true);
      return;
    }
    build_dir_ = cmake_build::ProjectBuildDir(project_root_, "release");
    if (!cmake_build::TryAcquireProjectBuild(project_root_)) {
      Finish(
          "A project build is already running. Wait for it to finish before "
          "building the game.",
          true);
      return;
    }
    holds_build_lock_ = true;
  }
  output_dir_ = output_dir;
  cancelled_ = false;
  console_.Add(
      Console::Level::kInfo, Console::Source::kBuild,
      "Build Game: building a Release executable for " + output_dir_.string());

  config_ = MakeConfig();
  const bool from_bundle = !os::BundleResourcesDir().empty();
  const bool has_checkout =
      fs::exists(fs::path(config_->source_dir) / "CMakeLists.txt");
  // A development editor builds a fresh Release executable with CMake. A
  // released editor (running from its .app) always uses the player and engine
  // resources bundled with it, so packaging a game needs no compiler or
  // third-party rebuild and behaves the same on every machine.
  if (!from_bundle && (has_checkout || config_->native)) {
    StartBuild();
    return;
  }
  LocateExecutable();
  if (config_->executable.empty()) {
    if (has_checkout) {
      StartBuild();
      return;
    }
    Finish(
        "mercury_player was not found next to the editor, and there is no "
        "engine checkout to build it from. Install the complete editor "
        "release.",
        true);
    return;
  }
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Using the Release player bundled with the editor");
  StartPacking();
}

ReleaseBuild::Config ReleaseBuild::MakeConfig() const {
  Config config;
  config.source_dir = os::EngineSdkDir().root.string();
  config.build_target = build_target_;
  config.build_dir = build_dir_.string();
  config.native = !build_target_.empty();
  // Prefer the copies that ship with the editor; in a development build these
  // resolve to the checkout's UI resources and the build's materials folder.
  config.ui_resources = os::EngineUiResourceDir().string();
  config.materials_dir = os::EngineMaterialDir().string();
  const fs::path bundle = os::BundleResourcesDir();
  if (!bundle.empty()) {
    const fs::path engine = bundle / "engine";
    config.runtime.library_list = (engine / "player-libs.txt").string();
    config.runtime.frameworks_dir =
        (fs::path(os::ExecutableDir()).parent_path() / "Frameworks").string();
    config.runtime.icd_manifest =
        (engine / "icd.d" / "MoltenVK_icd.json").string();
  }
  std::string name;
  for (char c : project_name_) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      name.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else if (!name.empty() && name.back() != '_') {
      name.push_back('_');
    }
  }
  while (!name.empty() && name.back() == '_') name.pop_back();
  config.target_name = name.empty() ? "game" : name;
  return config;
}

void ReleaseBuild::LocateExecutable() {
  std::error_code ec;
  if (config_->native) {
    // CMake writes every executable to <build>/bin; the target name is the
    // executable name.
    const fs::path candidate =
        cmake_build::ProjectExecutable(build_dir_, config_->build_target);
    if (fs::is_regular_file(candidate, ec)) {
      config_->executable = candidate.string();
      return;
    }
    console_.Error("the build did not produce " + candidate.string());
    return;
  }
  // A packaged editor ships a Release player next to itself, so Build Game can
  // package games without an engine checkout or a compiler.
  const fs::path bundled = PlaySession::PlayerPath();
  if (!bundled.empty() && fs::is_regular_file(bundled, ec)) {
    config_->executable = bundled.string();
    return;
  }
  const fs::path built =
      cmake_build::ProjectExecutable(cmake_build::BuildDir(), "mercury_player");
  if (!built.empty() && fs::is_regular_file(built, ec)) {
    config_->executable = built.string();
    return;
  }
}

bool ReleaseBuild::Launch(const std::vector<std::string>& args) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, Console::Source::kBuild, "> " + command);

  process_ = std::make_unique<os::Process>();
  std::string error;
  fs::path working_dir =
      config_ ? fs::path(config_->source_dir) : project_root_;
  if (config_ && config_->native && !config_->build_dir.empty()) {
    working_dir = config_->build_dir;
  }
  if (!process_->Start(args, working_dir, &error)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool ReleaseBuild::StartBuild() {
  const bool native = config_ && config_->native;
  console_.Add(
      Console::Level::kInfo, Console::Source::kBuild,
      native ? "Building the C++ game executable with CMake; the first one "
               "takes a while."
             : "Building a Release mercury_player with CMake; the first one "
               "takes a while.");
  std::vector<std::string> args;
  configuring_ = false;
  if (native) {
    if (cmake_build::NeedsConfigure(project_root_, build_dir_)) {
      configuring_ = true;
      args = cmake_build::ConfigureArguments(
          project_root_, build_dir_, cmake_build::SdkCmakeDir(), "Release");
      // Shipping does not need the editor extension module.
      args.push_back("-DMERCURY_BUILD_EDITOR_SYSTEMS=OFF");
    } else {
      args = cmake_build::BuildArguments(build_dir_, config_->build_target);
    }
  } else {
    args =
        cmake_build::BuildArguments(cmake_build::BuildDir(), "mercury_player");
  }
  if (!Launch(args)) return false;
  state_ = State::kBuilding;
  return true;
}

void ReleaseBuild::StartPacking() {
  // Compile the scripts here, on the editor's thread. The compile replaces the
  // process-wide script module and the type registry that the inspector and
  // Add Component menu read, so it must not run on the packing thread.
  {
    BuildOptions options;
    options.release = true;
    std::string build_log;
    const bool built = ScriptBuilder::Build(project_root_, options, &build_log);
    std::istringstream lines(build_log);
    for (std::string line; std::getline(lines, line);) {
      console_.Add(Console::Level::kInfo, Console::Source::kBuild, line);
    }
    if (!built) {
      Finish("building the scripts failed", true);
      return;
    }
  }
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Packing assets into .mpack files...");
  state_ = State::kPacking;
  packing_ = std::async(std::launch::async, &ReleaseBuild::Pack, *config_,
                        project_root_, output_dir_, max_archive_bytes_);
}

ReleaseBuild::PackResult ReleaseBuild::Pack(Config config, fs::path root,
                                            fs::path output_dir,
                                            uint64_t max_archive_bytes) {
  PackResult result;
  auto fail = [&](std::string message) {
    result.error = std::move(message);
    return result;
  };
  // The scripts were compiled before this thread started (StartPacking); the
  // .as sources are packed with the game.
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) return fail("could not create " + output_dir.string());
  const fs::path output = fs::weakly_canonical(output_dir, ec);
  // The CMake build can take minutes after BuildGame checked the folder; make
  // sure it still resolves inside the project before deleting anything in it.
  {
    std::error_code root_ec;
    const fs::path real_root = fs::weakly_canonical(root, root_ec);
    const fs::path relative = output.lexically_relative(real_root);
    if (ec || root_ec || relative.empty() || relative == "." ||
        *relative.begin() == "..") {
      return fail("the output folder is not inside the project");
    }
  }

  // Archives from an earlier build would shadow or outlive the new ones.
  for (const auto& entry : fs::directory_iterator(output, ec)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() && (extension == mpack::kExtension ||
                                    extension == mpack::kIndexExtension)) {
      fs::remove(entry.path(), ec);
    }
  }

  // game.mpack: everything in the project but code, caches and builds.
  mpack::Writer game;
  uintmax_t game_bytes = 0;
  auto it = fs::recursive_directory_iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  if (ec) return fail("could not read " + root.string() + ": " + ec.message());
  for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec)
      return fail("could not read " + root.string() + ": " + ec.message());
    const fs::path& path = it->path();
    const std::string name = path.filename().string();
    const bool is_dir = it->is_directory(ec);
    const bool skip = name.starts_with('.') ||
                      (it.depth() == 0 && IsSkippedAtRoot(name)) ||
                      (is_dir && fs::weakly_canonical(path, ec) == output);
    if (skip) {
      if (is_dir) it.disable_recursion_pending();
      continue;
    }
    // A symlink can point anywhere (an SSH key, say); only files that really
    // live in the project go into an archive that gets shipped.
    if (it->is_symlink(ec) || ec) {
      result.log.push_back("Skipped symlink (not packed): " +
                           path.lexically_relative(root).generic_string());
      continue;
    }
    if (!it->is_regular_file(ec) || IsSourceFile(path)) continue;
    game.Add(path.lexically_relative(root).generic_string(), path);
    game_bytes += it->file_size(ec);
  }
  // engine.mpack: the default UI theme and fonts, and compiled materials.
  mpack::Writer engine;
  uintmax_t engine_bytes = 0;
  const fs::path ui = config.ui_resources;
  if (ui.empty() || !fs::is_directory(ui, ec)) {
    return fail("the engine's UI resources are missing (" + ui.string() + ")");
  }
  for (const auto& entry : fs::recursive_directory_iterator(ui, ec)) {
    if (!entry.is_regular_file()) continue;
    engine.Add(std::string(mpack::kEngineUiFolder) +
                   entry.path().lexically_relative(ui).generic_string(),
               entry.path());
    engine_bytes += entry.file_size(ec);
  }
  const fs::path materials = config.materials_dir;
  int built = 0;
  for (const auto& entry : fs::directory_iterator(materials, ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".filamat")
      continue;
    engine.Add(std::string(mpack::kEngineMaterialFolder) +
                   entry.path().filename().generic_string(),
               entry.path());
    engine_bytes += entry.file_size(ec);
    built++;
  }
  if (built == 0) {
    return fail("no compiled materials in " + materials.string());
  }

  std::string error;
  for (auto [writer, base, bytes] :
       {std::tuple{&game, mpack::kGamePackName, game_bytes},
        std::tuple{&engine, mpack::kEnginePackName, engine_bytes}}) {
    if (!writer->Write(output, base, max_archive_bytes, &error)) {
      return fail(error);
    }
    const auto [packed, archives] = ArchiveSizes(output, base);
    result.log.push_back(std::format(
        "{}{}: {} files, {} -> {}{}", base, mpack::kExtension,
        writer->GetEntryCount(), FormatBytes(bytes), FormatBytes(packed),
        archives > 1 ? std::format(" in {} archives", archives) : ""));
  }

  const fs::path exe = config.executable;
  const fs::path shipped =
      output / (config.target_name + exe.extension().string());
  fs::copy_file(exe, shipped, fs::copy_options::overwrite_existing, ec);
  if (ec) return fail("could not copy " + exe.string() + ": " + ec.message());
  result.log.push_back("Copied the player as " + shipped.filename().string());
  if (!CopyBundledRuntime(config.runtime, output, result.log, error)) {
    return fail(error);
  }
  result.ok = true;
  return result;
}

void ReleaseBuild::Finish(const std::string& message, bool error) {
  if (holds_build_lock_) {
    cmake_build::ReleaseProjectBuild(project_root_);
    holds_build_lock_ = false;
  }
  console_.Add(error ? Console::Level::kError : Console::Level::kInfo,
               Console::Source::kBuild, message);
  state_ = State::kIdle;
}

void ReleaseBuild::Cancel() {
  if (state_ == State::kPacking) {
    if (!cancelled_) {
      console_.Warn("Build Game cancelled; waiting for packing to stop");
    }
    cancelled_ = true;
    return;
  }
  if (!process_) {
    if (holds_build_lock_) {
      cmake_build::ReleaseProjectBuild(project_root_);
      holds_build_lock_ = false;
    }
    state_ = State::kIdle;
    return;
  }
  process_->Kill();
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  process_.reset();
  Finish("Build Game cancelled", false);
}

void ReleaseBuild::Update() {
  if (state_ == State::kPacking) {
    if (packing_.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      return;
    }
    PackResult result = packing_.get();
    if (cancelled_) {
      state_ = State::kIdle;
      return;
    }
    for (auto& line : result.log) {
      console_.Add(Console::Level::kInfo, Console::Source::kBuild, line);
    }
    if (!result.ok) return Finish("Build Game failed: " + result.error, true);
    return Finish("Game built to " + output_dir_.string(), false);
  }

  if (!process_) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  if (process_->IsRunning()) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  const int code = process_->GetExitCode().value_or(-1);
  process_.reset();

  switch (state_) {
    case State::kBuilding:
      if (configuring_) {
        configuring_ = false;
        if (code != 0)
          return Finish("Release build configuration failed", true);
        EditorUI::RefreshProjectCompileCommands(project_root_);
        StartBuild();
        break;
      }
      if (code != 0) return Finish("Release build failed", true);
      LocateExecutable();
      if (config_->executable.empty()) {
        return Finish(config_->native
                          ? "The Release build did not produce the C++ game "
                            "executable"
                          : "The Release build did not produce mercury_player",
                      true);
      }
      if (holds_build_lock_) {
        cmake_build::ReleaseProjectBuild(project_root_);
        holds_build_lock_ = false;
      }
      StartPacking();
      break;
    case State::kPacking:
    case State::kIdle:
      break;
  }
}

}  // namespace Mercury
