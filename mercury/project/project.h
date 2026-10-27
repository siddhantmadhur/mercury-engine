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
   * Absolute paths are returned unchanged and other relative paths are taken
   * relative to the project root.
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
