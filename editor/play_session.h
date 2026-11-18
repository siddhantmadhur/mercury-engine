// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * play_session.h
 *
 * Drives Build and Play from the editor. AngelScript projects use the
 * mercury_player build from the same checkout (next to the editor, or in
 * <build>/bin):
 *
 *   mercury_player <project.mproj> --build-scripts     (AngelScript compile)
 *     -> mercury_player <project.mproj> --dump-schema .mercury/scripts.mschema
 *     -> mercury_player <project.mproj> --map <current map>      (Play only)
 *
 * A project with C++ scripts names a CMake target in `[build] target`. Its
 * build runs `cmake --build <MERCURY_BUILD_DIR> --target <target>` and then
 * executes <build>/bin/<target> with the same arguments, so the native scripts
 * linked into that executable provide the schema and run the game. The build's
 * output (compiler messages) streams to the Console.
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

struct EditorSettings;

class PlaySession {
 public:
  enum class State { kIdle, kBuilding, kRunning };

  PlaySession(Console& console, const EditorSettings& settings)
      : console_(console), settings_(settings) {}

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

  // The player executable that ships with the editor, or the one the CMake
  // build put in <build>/bin. Empty if there is none.
  static std::filesystem::path PlayerPath();

 private:
  void Begin(const Project& project, bool run);
  bool StartBuild();
  bool StartGame();
  bool DumpSchema();
  bool Launch(const std::vector<std::string>& args, Console::Source source,
              const std::vector<std::string>& environment = {});
  void Finish(const std::string& message, bool error);

  Console& console_;
  const EditorSettings& settings_;
  std::unique_ptr<os::Process> process_;
  Console::Source source_ = Console::Source::kBuild;
  State state_ = State::kIdle;
  bool run_after_build_ = false;
  bool schema_updated_ = false;

  std::filesystem::path project_root_;
  std::filesystem::path project_file_;
  std::filesystem::path schema_path_;
  std::filesystem::path working_dir_;
  std::string map_;
  std::string build_target_;
  bool native_ = false;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_PLAY_SESSION_H
