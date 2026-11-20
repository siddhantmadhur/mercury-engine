// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * project_scaffold.h
 *
 * The generated build and editor files of a project:
 *
 *   CMakeLists.txt          user-owned after creation
 *   CMakePresets.json       user-owned after creation
 *   .clangd                 regenerated every time the project opens
 *   compile_commands.json   link to .mercury/build/debug/compile_commands.json
 *
 * Generating these files does not run project code. Call this when a project
 * is created and whenever one is opened.
 */
#ifndef MERCURY_EDITOR_PROJECT_SCAFFOLD_H
#define MERCURY_EDITOR_PROJECT_SCAFFOLD_H

#include <filesystem>

namespace Mercury {
class Project;
}

namespace Mercury::EditorUI {

/**
 * @brief Writes/refreshes a project's generated build and LSP files
 *
 * `CMakeLists.txt` and `CMakePresets.json` are written only when missing.
 * `.clangd` is rewritten on every call from the running engine SDK, and the
 * compile_commands link is refreshed when a database exists.
 */
void ScaffoldProject(const Project& project);

/**
 * @brief Points <root>/compile_commands.json at the debug build's database
 *
 * A no-op when the database does not exist yet. Called after every project
 * configure (including the ones Play and the systems loader run).
 */
void RefreshProjectCompileCommands(const std::filesystem::path& root);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_PROJECT_SCAFFOLD_H
