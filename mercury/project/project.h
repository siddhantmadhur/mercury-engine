// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * project.h
 *
 * A Mercury project is a folder with a .mproj file at its root (like Godot's
 * project.godot). Every path stored in project files is written as
 * "res://relative/path" and resolved against that folder.
 *
 *   [project]
 *   name = "Pong"
 *   main_map = "res://maps/main.mmap"
 *
 *   [window]
 *   title = "Pong"
 *   width = 1280
 *   height = 720
 *
 *   [build]
 *   output = "Builds/Pong"     where the editor's Build Game writes the game
 */
#ifndef MERCURY_PROJECT_PROJECT_H
#define MERCURY_PROJECT_PROJECT_H

#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "serialization/mtext.h"

namespace Mercury {

namespace fs = std::filesystem;

constexpr std::string_view kResPrefix = "res://";
constexpr std::string_view kProjectExtension = ".mproj";
constexpr std::string_view kMapExtension = ".mmap";
constexpr std::string_view kAssetExtension = ".masset";
// UI Toolkit documents and style sheets.
constexpr std::string_view kUxmlExtension = ".uxml";
constexpr std::string_view kUssExtension = ".uss";
// Input actions, edited in the Input Actions window (input/input_actions.h).
constexpr std::string_view kInputActionsExtension = ".minput";
// Generated per-project data (build info, script schema); never committed.
constexpr std::string_view kProjectCacheDir = ".mercury";

/**
 * @brief A Mercury project: the `.mproj` file and its root folder
 *
 * A project is a folder with a `.mproj` at its root, like Godot's
 * `project.godot`. Paths inside project files are written relative to this
 * folder as `res://...`; @ref Resolve turns them into filesystem paths and
 * @ref ToResPath does the reverse. The project also holds the settings every
 * scene needs: the main map, window size, gravity and tick rate, layer and tag
 * names, the input actions asset, import defaults, build output and the
 * optional CMake target that builds C++ gameplay.
 *
 * @par Minimal .mproj
 * @code{.ini}
 * [project]
 * name = "HelloMercury"
 * main_map = "res://maps/main.mmap"
 *
 * [window]
 * title = "Hello Mercury"
 * width = 1280
 * height = 720
 * @endcode
 *
 * `Project::Create` creates the file in memory; call @ref Save to write it.
 * The editor's New Project template uses this, and `RunPlayer` loads a
 * project before building scripts and creating a @ref MapScene.
 *
 * @see Mercury::Map, Mercury::AssetDatabase, Mercury::RunPlayer,
 * @ref getting_started
 */
class Project {
 public:
  /**
   * @brief Load a project from its .mproj file
   * @return nullptr (with `error` filled) if the file is missing or invalid
   */
  static std::shared_ptr<Project> Load(const fs::path& mproj_file,
                                       std::string* error = nullptr);

  /**
   * @brief Create a project file in memory; call Save() to write it
   */
  static std::shared_ptr<Project> Create(const fs::path& mproj_file,
                                         std::string name);

  bool Save() const;

  const fs::path& GetFile() const { return file_; }
  const fs::path& GetRoot() const { return root_; }
  fs::path GetCacheDir() const { return root_ / kProjectCacheDir; }

  std::string GetName() const;
  void SetName(std::string name);
  std::string GetMainMap() const;
  // [project] version, Unity's Application.version ("dev" if unset)
  std::string GetVersion() const;
  void SetMainMap(std::string res_path);
  std::string GetWindowTitle() const;
  int GetWindowWidth() const;
  int GetWindowHeight() const;
  // [build] output: a folder, absolute or relative to the root; empty if unset
  std::string GetBuildOutput() const;
  void SetBuildOutput(std::string path);
  // [build] target: a CMake target for a C++ script project's executable,
  // e.g. "cpp_hello". Empty for AngelScript-only projects, which run the
  // shared mercury_player.
  std::string GetBuildTarget() const;
  void SetBuildTarget(std::string target);
  // [build] systems_target: the CMake target that builds the project's editor
  // modules. Defaults to "<build target>_systems"; empty when there is no
  // build target.
  std::string GetBuildSystemsTarget() const;
  // [render] anti_aliasing: "none", "fxaa" or "taa" (default). A project that
  // only has the legacy `fxaa` bool maps it to "fxaa"/"none".
  std::string GetRenderAntiAliasing() const;
  void SetRenderAntiAliasing(std::string mode);
  // [render] fsr: AMD FSR 1 upscaling of the window's scene (default off)
  bool GetRenderFsr() const;
  void SetRenderFsr(bool enabled);
  // [render] fsr_quality: "high" or "ultra" (default)
  std::string GetRenderFsrQuality() const;
  void SetRenderFsrQuality(std::string quality);
  // [render] render_scale: fraction of the window size drawn when FSR is on,
  // clamped to 0.5-1 (default 0.75)
  float GetRenderScale() const;
  void SetRenderScale(float scale);
  // [render] fsr_sharpness: RCAS sharpening, clamped to 0-1 (default 0.9)
  float GetRenderFsrSharpness() const;
  void SetRenderFsrSharpness(float sharpness);
  // [physics] gravity, in units per second squared
  glm::vec3 GetGravity() const;
  // [physics] tick_rate: fixed ticks (OnTick + physics steps) per second
  int GetTickRate() const;
  // [physics] queries_hit_triggers: raycasts hit triggers unless told not to
  bool GetQueriesHitTriggers() const;
  // [import] model_convention: "native" (default) or "unity"; see
  // properties/model.h
  std::string GetModelConvention() const;
  // [input] actions: res:// path of the project's .minput, or ""
  std::string GetInputActionsPath() const;
  void SetInputActionsPath(std::string res_path);

  /**
   * @brief Layer names from [layers] names = ["Default", ...] (index = layer)
   *
   * Without that section: Unity's built-in layers.
   */
  std::vector<std::string> GetLayerNames() const;
  std::string LayerName(int layer) const;
  int LayerFromName(std::string_view name) const;  // -1 if unknown
  // Tags from [tags] names = [...], always including Unity's built-ins.
  std::vector<std::string> GetTags() const;

  /**
   * @brief Turn "res://a/b.mmap" into an absolute path inside the project
   *
   * Relative paths are taken relative to the project root. The result must
   * stay inside the project folder, symlinks followed: absolute paths outside
   * it, `..` escapes and symlinks that lead out all yield an empty path, so
   * a project file cannot make the engine read or write elsewhere. Check for
   * an empty result before using it.
   */
  fs::path Resolve(std::string_view path) const;

  /**
   * @brief Turn a path inside the project into "res://..." form
   * @return An empty string if the path is outside the project folder
   */
  std::string ToResPath(const fs::path& path) const;

  // Every setting, for sections other code (or the user) adds.
  mtext::Document& GetDocument() { return document_; }
  const mtext::Document& GetDocument() const { return document_; }

 private:
  mtext::Section& GetOrAddSection(std::string_view tag);

  fs::path file_;
  fs::path root_;
  mtext::Document document_;
};

bool IsResPath(std::string_view path);

}  // namespace Mercury

#endif  // MERCURY_PROJECT_PROJECT_H
