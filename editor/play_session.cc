#include "play_session.h"

#include <chrono>
#include <format>
#include <thread>

#include "scripting/type_registry.h"
#include "serialization/mtext.h"

namespace Mercury {

namespace fs = std::filesystem;

fs::path PlaySession::SchemaPath(const Project& project) {
  return project.GetCacheDir() / "scripts.mschema";
}

void PlaySession::Play(const Project& project, const std::string& map_res_path) {
  map_ = map_res_path;
  Begin(project, true);
}

void PlaySession::Build(const Project& project) { Begin(project, false); }

void PlaySession::Begin(const Project& project, bool run) {
  if (IsBusy()) return;
  project_root_ = project.GetRoot();
  project_file_ = project.GetFile();
  schema_path_ = SchemaPath(project);
  run_after_build_ = run;

  config_ = ReadPlayerConfig();
  if (config_) {
    StartBuild();
  } else {
    StartConfigure();
  }
}

std::optional<PlaySession::PlayerConfig> PlaySession::ReadPlayerConfig() const {
  const fs::path cache = project_root_ / kProjectCacheDir;
  std::vector<fs::path> candidates = {cache / "player.mcfg",
                                      cache / "player-Debug.mcfg",
                                      cache / "player-RelWithDebInfo.mcfg",
                                      cache / "player-Release.mcfg"};
  for (const auto& file : candidates) {
    mtext::Document document;
    if (!mtext::LoadFile(file, document)) continue;
    auto section = document.Find("player");
    if (!section) continue;
    PlayerConfig config;
    config.executable = section->properties.GetString("executable");
    config.build_dir = section->properties.GetString("build_dir");
    config.target = section->properties.GetString("target");
    config.config = section->properties.GetString("config");
    if (!config.executable.empty() && !config.build_dir.empty()) return config;
  }
  return std::nullopt;
}

bool PlaySession::Launch(const std::vector<std::string>& args,
                         Console::Source source) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, source, "> " + command);

  process_ = std::make_unique<os::Process>();
  source_ = source;
  std::string error;
  if (!process_->Start(args, project_root_, &error)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool PlaySession::StartConfigure() {
  if (!fs::exists(project_root_ / "CMakeLists.txt")) {
    Finish(
        "This project has no build yet and no CMakeLists.txt. Configure it with "
        "CMake (mercury_add_game) so .mercury/player.mcfg exists.",
        true);
    return false;
  }
  console_.Info("No build found for this project; configuring it with CMake.");
  std::vector<std::string> args = {
      "cmake", "-S", project_root_.string(), "-B",
      (project_root_ / kProjectCacheDir / "build").string(),
      "-DCMAKE_BUILD_TYPE=Debug"};
  if (os::IsOnPath("ninja")) {
    args.insert(args.begin() + 1, {"-G", "Ninja"});
  }
  if (!Launch(args, Console::Source::kBuild)) return false;
  state_ = State::kConfiguring;
  return true;
}

bool PlaySession::StartBuild() {
  std::vector<std::string> args = {"cmake", "--build", config_->build_dir,
                                   "--target", config_->target};
  if (!config_->config.empty()) {
    args.push_back("--config");
    args.push_back(config_->config);
  }
  if (!Launch(args, Console::Source::kBuild)) return false;
  state_ = State::kBuilding;
  return true;
}

bool PlaySession::DumpSchema() {
  os::Process dump;
  std::string error;
  if (!dump.Start({config_->executable, "--dump-schema", schema_path_.string()},
                  project_root_, &error)) {
    console_.Error("Could not read the game's scripts: " + error);
    return false;
  }
  // Writing the schema takes milliseconds; wait at most a few seconds.
  for (int i = 0; i < 500 && dump.IsRunning(); i++) {
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
  std::vector<std::string> args = {config_->executable, project_file_.string()};
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
    case State::kConfiguring:
      if (code != 0) return Finish("CMake configure failed", true);
      config_ = ReadPlayerConfig();
      if (!config_) {
        return Finish(
            "CMake ran but did not write .mercury/player.mcfg; does "
            "CMakeLists.txt call mercury_add_game()?",
            true);
      }
      StartBuild();
      break;
    case State::kBuilding:
      if (code != 0) return Finish("Build failed", true);
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
