// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "play_session.h"

#include <chrono>
#include <format>
#include <thread>

#include "cmake_build.h"
#include "os/filesystem.h"
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
  run_after_build_ = run;
  if (native_) {
    working_dir_ = cmake_build::BuildDir();
    std::error_code ec;
    if (working_dir_.empty() ||
        !fs::exists(working_dir_ / "CMakeCache.txt", ec)) {
      Finish(
          "This project names a C++ build target, but the engine has no "
          "configured CMake build directory. Run `cmake --preset release` in "
          "the engine checkout first.",
          true);
      return;
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
                         Console::Source source) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, source, "> " + command);

  process_ = std::make_unique<os::Process>();
  source_ = source;
  std::string error;
  if (!process_->Start(args, working_dir_, &error)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool PlaySession::StartBuild() {
  std::vector<std::string> args;
  if (native_) {
    args = cmake_build::BuildArguments(build_target_);
  } else {
    args = {PlayerPath().string(), project_file_.string(), "--build-scripts"};
  }
  if (!Launch(args, Console::Source::kBuild)) return false;
  state_ = State::kBuilding;
  return true;
}

bool PlaySession::DumpSchema() {
  std::vector<std::string> args;
  if (native_) {
    args = {cmake_build::Executable(build_target_).string(),
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
  std::vector<std::string> args;
  if (native_) {
    args = {cmake_build::Executable(build_target_).string(),
            project_file_.string()};
  } else {
    args = {PlayerPath().string(), project_file_.string()};
  }
  if (!map_.empty()) {
    args.push_back("--map");
    args.push_back(map_);
  }
  if (!Launch(args, Console::Source::kGame)) return false;
  state_ = State::kRunning;
  return true;
}

void PlaySession::Finish(const std::string& message, bool error) {
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
      if (code != 0) return Finish("Build failed: fix the errors above", true);
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
