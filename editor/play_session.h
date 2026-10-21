/*
 * play_session.h
 *
 * Drives Build and Play from the editor:
 *
 *   configure (only if the project has no build yet)
 *     -> cmake --build <build_dir> --target <game>
 *     -> <game> --dump-schema .mercury/scripts.mschema
 *     -> <game> <project.mproj> --map <current map>      (Play only)
 *
 * Where to build comes from .mercury/player.mcfg, which mercury_add_game()
 * writes when CMake configures the project.
 */
#ifndef MERCURY_EDITOR_PLAY_SESSION_H
#define MERCURY_EDITOR_PLAY_SESSION_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "console.h"
#include "os/process.h"
#include "project/project.h"

namespace Mercury {

class PlaySession {
 public:
  enum class State { kIdle, kConfiguring, kBuilding, kRunning };

  explicit PlaySession(Console& console) : console_(console) {}

  void Play(const Project& project, const std::string& map_res_path);
  void Build(const Project& project);
  void Stop();

  // Call every frame: forwards process output and moves to the next step.
  void Update();

  State GetState() const { return state_; }
  bool IsBusy() const { return state_ != State::kIdle; }
  bool IsRunning() const { return state_ == State::kRunning; }

  // True once after a build refreshed .mercury/scripts.mschema.
  bool TakeSchemaUpdated();

  static std::filesystem::path SchemaPath(const Project& project);

 private:
  struct PlayerConfig {
    std::string executable;
    std::string build_dir;
    std::string target;
    std::string config;
  };

  void Begin(const Project& project, bool run);
  std::optional<PlayerConfig> ReadPlayerConfig() const;
  bool StartConfigure();
  bool StartBuild();
  bool StartGame();
  bool DumpSchema();
  bool Launch(const std::vector<std::string>& args, Console::Source source);
  void Finish(const std::string& message, bool error);

  Console& console_;
  std::unique_ptr<os::Process> process_;
  Console::Source source_ = Console::Source::kBuild;
  State state_ = State::kIdle;
  bool run_after_build_ = false;
  bool schema_updated_ = false;

  std::filesystem::path project_root_;
  std::filesystem::path project_file_;
  std::filesystem::path schema_path_;
  std::string map_;
  std::optional<PlayerConfig> config_;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_PLAY_SESSION_H
