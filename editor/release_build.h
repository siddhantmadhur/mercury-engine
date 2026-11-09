/*
 * release_build.h
 *
 * Project > Build Game: makes a folder you can ship, like Source 2's release
 * builds with their .vpk files:
 *
 *   <output>/<game>(.exe)
 *   <output>/engine.mindex    index of engine.mpack
 *   <output>/engine.mpack     compiled shaders, default UI theme and fonts
 *   <output>/game.mindex      index of game.mpack and its game_NNN.mpack
 *   <output>/game.mpack       the project's maps, assets and UI
 *                             (game_001.mpack, ... once game.mpack is full;
 *                             the size is chosen in the Build Game dialog)
 *
 * Steps, each started from Update():
 *
 *   build a Release mercury_player with Bazel, in the engine checkout
 *     -> bazel build -c opt //mercury:mercury_player
 *     -> on a worker thread: minify the scripts (esbuild), pack the files, copy the player
 *
 * The scripts are packed as the minified .mercury/scripts/game.js; the .ts sources stay out. The
 * executable finds game.mpack next to itself and runs from it (see RunPlayer). Build and Play
 * are unchanged and keep using loose files.
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
#include "os/process.h"
#include "project/project.h"

namespace Mercury {

class ReleaseBuild {
 public:
  enum class State { kIdle, kBuilding, kPacking };

  explicit ReleaseBuild(Console& console) : console_(console) {}
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

 private:
  struct Config {
    std::string executable;  // the Release mercury_player
    std::string source_dir;  // the engine checkout
    std::string target_name;  // what the shipped executable is called
    std::string spirv_dir;
    std::string ui_resources;
  };
  struct PackResult {
    bool ok = false;
    std::vector<std::string> log;
    std::string error;
  };

  // Where the Release player will be, from the engine checkout this editor was built from.
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
  std::unique_ptr<os::Process> process_;
  std::future<PackResult> packing_;
  State state_ = State::kIdle;
  bool cancelled_ = false;

  std::filesystem::path project_root_;
  std::string project_name_;
  std::filesystem::path output_dir_;
  uint64_t max_archive_bytes_ = 0;
  std::optional<Config> config_;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_RELEASE_BUILD_H
