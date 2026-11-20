// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * templates.h
 *
 * Files the editor creates: new projects, scripts and assets.
 */
#ifndef MERCURY_EDITOR_TEMPLATES_H
#define MERCURY_EDITOR_TEMPLATES_H

#include <filesystem>
#include <string>
#include <string_view>

namespace Mercury::Templates {

/**
 * @brief The kinds of script the New Script dialog can create
 *
 * AngelScript and C++ Behaviour are gameplay components (scripts/ and src/);
 * a C++ System is a scene-wide subsystem (src/); a C++ Editor System is a
 * module the editor loads from system/.
 */
enum class ScriptKind {
  kAngelScript,
  kCppBehaviour,
  kCppSystem,
  kCppEditorSystem,
};

// Settings/config spelling, e.g. "cpp_behaviour".
const char* ScriptKindName(ScriptKind kind);
ScriptKind ScriptKindFromName(std::string_view name);
bool IsCppScript(ScriptKind kind);
bool IsEditorSystem(ScriptKind kind);

// The files a script of `kind` is made of. `header` is empty for AngelScript.
struct ScriptPaths {
  std::filesystem::path header;
  std::filesystem::path source;
};

ScriptPaths GetScriptPaths(const std::filesystem::path& project_root,
                           ScriptKind kind, const std::string& class_name);

/**
 * @brief Create `<parent>/<name>/` with a .mproj, a main map, an AngelScript
 * scripts folder, and empty folders
 * @return The .mproj path, or empty with `error` filled
 */
std::filesystem::path CreateProject(const std::filesystem::path& parent,
                                    const std::string& name, bool is_3d,
                                    std::string* error);

/**
 * @brief Write `scripts/<snake_name>.as` with a [script] Behaviour class
 * @return The .as path, or empty with `error` filled
 */
std::filesystem::path CreateScript(const std::filesystem::path& project_root,
                                   const std::string& class_name,
                                   std::string* error);

/**
 * @brief Write the files a script kind is made of, never overwriting
 * @return The .as/.cc path (the file to open), or empty with `error` filled
 */
std::filesystem::path CreateScript(const std::filesystem::path& project_root,
                                   const std::string& class_name,
                                   ScriptKind kind, std::string* error);

// A UI document with a root VisualElement, and an empty style sheet.
bool CreateUxml(const std::filesystem::path& file);
bool CreateUss(const std::filesystem::path& file);

// Unity's default actions: a Player map with Move, Look, Jump and Fire.
bool CreateInputActions(const std::filesystem::path& file);

// Writes a .masset of `type` with every field at its default value.
bool CreateAsset(const std::filesystem::path& file, const std::string& type);

// A map with a root node and a 2D camera, or for 3D a camera, a light, a
// ground plane and a cube.
std::string NewMapText(const std::string& root_name, bool is_3d);

bool IsValidIdentifier(const std::string& name);
std::string ToSnakeCase(const std::string& name);
// A CMake target for a project name: lower case, underscores, "game_" before
// a leading digit. Used for the C++ build target default.
std::string ToTargetName(const std::string& name);

}  // namespace Mercury::Templates

#endif  // MERCURY_EDITOR_TEMPLATES_H
