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
 */
#ifndef MERCURY_PROJECT_PROJECT_H
#define MERCURY_PROJECT_PROJECT_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "serialization/mtext.h"

namespace Mercury {

namespace fs = std::filesystem;

constexpr std::string_view kResPrefix = "res://";
constexpr std::string_view kProjectExtension = ".mproj";
constexpr std::string_view kMapExtension = ".mmap";
constexpr std::string_view kAssetExtension = ".masset";
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
  void SetMainMap(std::string res_path);
  std::string GetWindowTitle() const;
  int GetWindowWidth() const;
  int GetWindowHeight() const;

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
