// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * release_build.h
 *
 * Project > Build Game: makes a folder you can ship, like Source 2's release
 * builds with their .vpk files:
 *
 *   <output>/<game>(.exe)
 *   <output>/engine.mindex    index of engine.mpack
 *   <output>/engine.mpack     compiled materials, default UI theme and fonts
 *   <output>/game.mindex      index of game.mpack and its game_NNN.mpack
 *   <output>/game.mpack       the project's maps, assets and UI
 *                             (game_001.mpack, ... once game.mpack is full;
 *                             the size is chosen in the Build Game dialog)
 *
 * Steps, each started from Update():
 *
 *   development editor (an engine checkout is present)
 *     -> cmake --build <build-dir> --target mercury_player
 *     -> or, for C++ scripts, cmake --build <build-dir> --target <project
 * target> released editor (no checkout: the player and engine resources ship in
 * the .app)
 *     -> use Contents/MacOS/mercury_player and
 * Contents/Resources/{ui,materials} then: compile the scripts (on the editor's
 * thread), and on a worker thread pack the files, copy the player and the
 * runtime libraries it needs
 *
 * The .as scripts are packed as sources; the executable finds game.mpack next
 * to itself and runs from it (see RunPlayer). Build and Play are unchanged and
 * keep using loose files.
 */
#ifndef MERCURY_EDITOR_RELEASE_BUILD_H
#define MERCURY_EDITOR_RELEASE_BUILD_H

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "editor_settings.h"
#include "os/process.h"
#include "project/project.h"

namespace Mercury {

class ReleaseBuild {
 public:
  enum class State { kIdle, kBuilding, kPacking };

  explicit ReleaseBuild(Console& console, const EditorSettings& settings)
      : console_(console), settings_(settings) {}
  ~ReleaseBuild();

  // max_archive_bytes: size at which game.mpack rolls over into game_001.mpack
  // and so on, 0 for a single archive.
  void Start(const Project& project, const std::filesystem::path& output_dir,
             uint64_t max_archive_bytes);
  // Stops the configure or build. Packing finishes, but its result is
  // dropped.
  void Cancel();

  // Call every frame: forwards process output and moves to the next step.
  void Update();

  State GetState() const { return state_; }
  bool IsBusy() const { return state_ != State::kIdle; }

  // "Builds/<project name>", relative to the project root.
  static std::string DefaultOutput(const Project& project);

  // The runtime support a packaged editor carries for its player: the list of
  // dylibs it needs and the Vulkan ICD manifest for a plain game folder.
  struct Runtime {
    std::string
        library_list;  // one dylib file name per line, relative to Frameworks
    std::string frameworks_dir;  // the editor bundle's Contents/Frameworks
    std::string icd_manifest;    // MoltenVK_icd.json written for a game folder
  };

 private:
  struct Config {
    std::string executable;   // the Release mercury_player, or the game exe
    std::string source_dir;   // the engine checkout
    std::string target_name;  // what the shipped executable is called
    std::string materials_dir;
    std::string ui_resources;
    // C++ script projects: a CMake target and the build directory that holds
    // it.
    std::string build_target;
    std::string build_dir;
    bool native = false;
    Runtime runtime;
  };
  struct PackResult {
    bool ok = false;
    std::vector<std::string> log;
    std::string error;
  };

  // Where the Release player will be, from the engine checkout this editor was
  // built from.
  Config MakeConfig() const;
  // Finds the built player.
  void LocateExecutable();
  bool StartBuild();
  void StartPacking();
  static PackResult Pack(Config config, std::filesystem::path project_root,
                         std::filesystem::path output_dir,
                         uint64_t max_archive_bytes);
  bool Launch(const std::vector<std::string>& args);
  void Finish(const std::string& message, bool error);

  Console& console_;
  const EditorSettings& settings_;
  std::unique_ptr<os::Process> process_;
  std::future<PackResult> packing_;
  State state_ = State::kIdle;
  bool cancelled_ = false;
  // The native project build starts with a configure step when its cache is
  // missing or stale.
  bool configuring_ = false;
  bool holds_build_lock_ = false;

  std::filesystem::path project_root_;
  std::string project_name_;
  std::filesystem::path output_dir_;
  uint64_t max_archive_bytes_ = 0;
  std::string build_target_;
  std::filesystem::path build_dir_;
  std::optional<Config> config_;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_RELEASE_BUILD_H
