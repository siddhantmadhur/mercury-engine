/*
 * editor_settings.h
 *
 * Per-user editor preferences, stored in the mtext format at
 * ~/.config/mercury/editor.cfg (%APPDATA%\Mercury\editor.cfg on Windows):
 *
 *   [editor]
 *   external_editor = "code {project} {file}"
 *
 *   [recent]
 *   projects = ["/home/me/games/pong/pong.mproj"]
 *
 *   [windows]
 *   ui_builder = true
 *   lore = true
 *
 *   [lore]
 *   executable = "lore"
 *   server = "lore://127.0.0.1:41337"
 */
#ifndef MERCURY_EDITOR_EDITOR_SETTINGS_H
#define MERCURY_EDITOR_EDITOR_SETTINGS_H

#include <filesystem>
#include <string>
#include <vector>

namespace Mercury {

struct EditorSettings {
  // Command used to open scripts. {file} and {project} are replaced with
  // paths. Empty means the desktop's default application.
  std::string external_editor;
  std::vector<std::string> recent_projects;
  // Window > UI Builder.
  bool show_ui_builder = false;
  // Window > Lore.
  bool show_lore = true;
  // The Lore command line tool; empty means "lore" from PATH.
  std::string lore_executable;
  // Server new Lore repositories are created on.
  std::string lore_server = "lore://127.0.0.1:41337";

  static std::filesystem::path ConfigDir();
  static std::filesystem::path HomeDir();
  void Load();
  void Save() const;
  void AddRecentProject(const std::filesystem::path& mproj);
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_EDITOR_SETTINGS_H
