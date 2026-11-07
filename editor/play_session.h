/*
 * play_session.h
 *
 * Drives Build and Play from the editor, with the mercury_player that sits next to the editor:
 *
 *   mercury_player <project.mproj> --build-scripts     (esbuild, and tsc when installed)
 *     -> mercury_player <project.mproj> --dump-schema .mercury/scripts.mschema
 *     -> mercury_player <project.mproj> --map <current map>      (Play only)
 *
 * The build's output (esbuild and tsc messages) streams to the Console.
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
  enum class State { kIdle, kBuilding, kRunning };

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
  void Begin(const Project& project, bool run);
  bool StartBuild();
  bool StartGame();
  bool DumpSchema();
  bool Launch(const std::vector<std::string>& args, Console::Source source);
  void Finish(const std::string& message, bool error);
  // The player executable next to the editor.
  static std::filesystem::path PlayerPath();

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
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_PLAY_SESSION_H
