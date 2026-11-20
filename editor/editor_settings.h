// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * editor_settings.h
 *
 * Per-user editor preferences, stored in the mtext format at
 * ~/.config/mercury/editor.cfg (%APPDATA%\Mercury\editor.cfg on Windows):
 *
 *   [editor]
 *   external_editor = "code {project} {file}"
 *   open_scripts_in_editor = true
 *   open_script_after_create = true
 *   new_script_kind = "angel_script"
 *   vim_mode = false
 *   show_metal_hud = false
 *   pack_size_mb = 256
 *
 *   [recent]
 *   projects = ["/home/me/games/pong/pong.mproj"]
 *
 *   [windows]
 *   ui_builder = true
 *   input_actions = false
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

#include "templates.h"

namespace Mercury {

struct EditorSettings {
  // Command used to open scripts. {file} and {project} are replaced with
  // paths. Empty means the desktop's default application.
  std::string external_editor;
  // Open .as files in the built-in script editor instead of an external tool.
  bool open_scripts_in_editor = true;
  // New Script dialog: open the created .as/.cc when the user clicks Create.
  bool open_script_after_create = true;
  // New Script dialog: the kind selected last time (remembered across runs).
  Templates::ScriptKind new_script_kind = Templates::ScriptKind::kAngelScript;
  // Enable modal Vim keybindings in the built-in script editor.
  bool vim_mode = false;
#if defined(__APPLE__)
  // macOS Debug builds: set MTL_HUD_ENABLED=1 when Play launches the game, so
  // Metal's FPS and GPU statistics overlay is visible over the game window.
  bool show_metal_hud = false;
#endif
  // Build Game: size at which game.mpack rolls over into the next archive,
  // in MiB. 0 keeps the whole pack in one archive.
  int pack_size_mb = 256;
  std::vector<std::string> recent_projects;
  // Window > UI Builder.
  bool show_ui_builder = false;
  // Window > Input Actions.
  bool show_input_actions = false;
  // Window > Lore.
  bool show_lore = true;
  // The Lore command line tool; empty means "lore" from PATH.
  std::string lore_executable;
  // Server new Lore repositories are created on.
  std::string lore_server = "lore://127.0.0.1:41337";
  // Canonical project roots whose editor systems the user trusted or refused.
  // A project in neither list has not been asked about yet.
  std::vector<std::string> trusted_projects;
  std::vector<std::string> distrusted_projects;

  // Whether the user already answered the trust prompt for this project.
  bool HasProjectTrustDecision(const std::filesystem::path& root) const;
  // True only when the user explicitly trusted the project. An unknown
  // project is not trusted.
  bool IsProjectTrusted(const std::filesystem::path& root) const;
  // Records (and replaces) the decision for a project.
  void SetProjectTrusted(const std::filesystem::path& root, bool trusted);
  // The path key used in the config: weak canonical form when it exists.
  static std::string CanonicalProjectPath(const std::filesystem::path& root);

  static std::filesystem::path ConfigDir();
  static std::filesystem::path HomeDir();
  void Load();
  void Save() const;
  void AddRecentProject(const std::filesystem::path& mproj);
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_EDITOR_SETTINGS_H
