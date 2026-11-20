// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "play_session.h"

#include <chrono>
#include <format>
#include <thread>

#include "cmake_build.h"
#include "editor_settings.h"
#include "os/filesystem.h"
#include "project_scaffold.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace fs = std::filesystem;

fs::path PlaySession::SchemaPath(const Project& project) {
  return project.GetCacheDir() / "scripts.mschema";
}

void PlaySession::Play(const Project& project,
                       const std::string& map_res_path) {
  map_ = map_res_path;
  Begin(project, true);
}

void PlaySession::Build(const Project& project) { Begin(project, false); }

fs::path PlaySession::PlayerPath() {
#if defined(_WIN32)
  const char* file = "mercury_player.exe";
#else
  const char* file = "mercury_player";
#endif
  const fs::path dir = os::ExecutableDir();
  std::error_code ec;
  // A packaged editor puts the player next to itself. A development build
  // puts both in <build>/bin.
  std::vector<fs::path> candidates = {dir / file};
  const fs::path build_dir = cmake_build::BuildDir();
  if (!build_dir.empty()) candidates.push_back(build_dir / "bin" / file);
  for (const fs::path& candidate : candidates) {
    if (fs::exists(candidate, ec)) return candidate;
  }
  return {};
}

void PlaySession::Begin(const Project& project, bool run) {
  if (IsBusy()) return;
  project_root_ = project.GetRoot();
  project_file_ = project.GetFile();
  schema_path_ = SchemaPath(project);
  working_dir_ = project_root_;
  build_target_ = project.GetBuildTarget();
  native_ = !build_target_.empty();
  built_this_session_ = false;
  run_after_build_ = run;
  if (native_) {
    // Building the project's CMake tree runs project code at configure time.
    // The UI asks for trust first; this gate makes a caller that skipped the
    // prompt fail closed.
    if (!settings_.IsProjectTrusted(project_root_)) {
      Finish(
          "This project is not trusted, so its C++ build was not run. Use "
          "Project > Trust Editor Systems and accept the prompt first.",
          true);
      return;
    }
    const fs::path sdk_cmake = cmake_build::SdkCmakeDir();
    if (sdk_cmake.empty()) {
      Finish(
          "This project names a C++ build target, but the editor's SDK has no "
          "CMake package. A development checkout must be configured (and "
          "built) once; a packaged editor should include Contents/Resources/"
          "sdk.",
          true);
      return;
    }
    build_dir_ = cmake_build::ProjectBuildDir(project_root_);
    working_dir_ = build_dir_;
    if (!cmake_build::TryAcquireProjectBuild(project_root_)) {
      Finish(
          "A project build is already running. Wait for it to finish before "
          "pressing Play.",
          true);
      return;
    }
    holds_build_lock_ = true;
    if (cmake_build::NeedsConfigure(project_root_, build_dir_)) {
      console_.Info("Configuring the project build in " + build_dir_.string());
    }
  } else if (PlayerPath().empty()) {
    Finish(
        "mercury_player was not found next to the editor. Install the complete "
        "editor release, or build the mercury_player target in the engine "
        "checkout.",
        true);
    return;
  }
  StartBuild();
}

bool PlaySession::Launch(const std::vector<std::string>& args,
                         Console::Source source,
                         const std::vector<std::string>& environment) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, source, "> " + command);

  process_ = std::make_unique<os::Process>();
  source_ = source;
  std::string error;
  if (!process_->Start(args, working_dir_, &error, environment)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool PlaySession::StartBuild() {
  std::vector<std::string> args;
  configuring_ = false;
  if (native_) {
    if (cmake_build::NeedsConfigure(project_root_, build_dir_)) {
      configuring_ = true;
      args = cmake_build::ConfigureArguments(project_root_, build_dir_,
                                             cmake_build::SdkCmakeDir());
    } else {
      args = cmake_build::BuildArguments(build_dir_, build_target_);
    }
  } else {
    args = {PlayerPath().string(), project_file_.string(), "--build-scripts"};
  }
  if (!Launch(args, Console::Source::kBuild)) return false;
  state_ = State::kBuilding;
  return true;
}

bool PlaySession::DumpSchema() {
  if (native_ && !built_this_session_) {
    console_.Error(
        "Refusing to run a game executable that this Play session did not "
        "build.");
    return false;
  }
  std::vector<std::string> args;
  if (native_) {
    args = {cmake_build::ProjectExecutable(build_dir_, build_target_).string(),
            project_file_.string(), "--dump-schema", schema_path_.string()};
  } else {
    args = {PlayerPath().string(), project_file_.string(), "--dump-schema",
            schema_path_.string()};
  }
  os::Process dump;
  std::string error;
  if (!dump.Start(args, working_dir_, &error)) {
    console_.Error("Could not read the game's scripts: " + error);
    return false;
  }
  // Writing the schema takes milliseconds, but a cold `cmake --build` may
  // compile the game. Wait at most 30 seconds.
  for (int i = 0; i < 3000 && dump.IsRunning(); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (dump.IsRunning()) dump.Kill();
  for (auto& line : dump.TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  if (dump.GetExitCode().value_or(1) != 0) {
    console_.Error("The game could not write its script list");
    return false;
  }
  schema_updated_ = true;
  return true;
}

bool PlaySession::StartGame() {
  if (native_ && !built_this_session_) {
    console_.Error(
        "Refusing to run a game executable that this Play session did not "
        "build.");
    return false;
  }
  std::vector<std::string> args;
  if (native_) {
    args = {cmake_build::ProjectExecutable(build_dir_, build_target_).string(),
            project_file_.string()};
  } else {
    args = {PlayerPath().string(), project_file_.string()};
  }
  if (!map_.empty()) {
    args.push_back("--map");
    args.push_back(map_);
  }
  std::vector<std::string> environment;
#if defined(__APPLE__)
  if (cmake_build::IsDebugBuild() && settings_.show_metal_hud) {
    environment.emplace_back("MTL_HUD_ENABLED=1");
  }
#endif
  if (!Launch(args, Console::Source::kGame, environment)) return false;
  state_ = State::kRunning;
  return true;
}

void PlaySession::Finish(const std::string& message, bool error) {
  if (holds_build_lock_) {
    cmake_build::ReleaseProjectBuild(project_root_);
    holds_build_lock_ = false;
  }
  if (!message.empty()) {
    if (error) {
      console_.Error(message);
    } else {
      console_.Info(message);
    }
  }
  state_ = State::kIdle;
}

void PlaySession::Stop() {
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
    console_.AddProcessOutput(source_, std::move(line));
  }
  process_.reset();
  Finish(state_ == State::kRunning ? "Game stopped" : "Build cancelled", false);
}

void PlaySession::Update() {
  if (!process_) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(source_, std::move(line));
  }
  if (process_->IsRunning()) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(source_, std::move(line));
  }
  const int code = process_->GetExitCode().value_or(-1);
  process_.reset();

  switch (state_) {
    case State::kBuilding:
      if (configuring_) {
        if (code != 0) {
          return Finish("Build configuration failed: fix the errors above",
                        true);
        }
        configuring_ = false;
        EditorUI::RefreshProjectCompileCommands(project_root_);
        // The project's CMakeLists and presets are current now; the build
        // step follows as its own child process.
        StartBuild();
        break;
      }
      if (code != 0) return Finish("Build failed: fix the errors above", true);
      if (holds_build_lock_) {
        cmake_build::ReleaseProjectBuild(project_root_);
        holds_build_lock_ = false;
      }
      built_this_session_ = true;
      DumpSchema();
      if (run_after_build_) {
        console_.Info("Build succeeded; starting the game");
        StartGame();
      } else {
        Finish("Build succeeded", false);
      }
      break;
    case State::kRunning:
      Finish(std::format("Game exited with code {}", code), code != 0);
      break;
    case State::kIdle:
      break;
  }
}

bool PlaySession::TakeSchemaUpdated() {
  bool updated = schema_updated_;
  schema_updated_ = false;
  return updated;
}

}  // namespace Mercury
