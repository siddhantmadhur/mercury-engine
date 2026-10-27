/*
 * templates.h
 *
 * Files the editor creates: new projects, scripts and assets.
 */
#ifndef MERCURY_EDITOR_TEMPLATES_H
#define MERCURY_EDITOR_TEMPLATES_H

#include <filesystem>
#include <string>

namespace Mercury::Templates {

/**
 * @brief Create <parent>/<name>/ with a .mproj, a main map, a CMakeLists.txt
 * that builds the game against this engine checkout, and empty folders
 * @return The .mproj path, or empty with `error` filled
 */
std::filesystem::path CreateProject(const std::filesystem::path& parent,
                                    const std::string& name, bool is_3d,
                                    std::string* error);

/**
 * @brief Write scripts/<snake_name>.h and .cc for a Behaviour subclass
 * @return The .cc path, or empty with `error` filled
 */
std::filesystem::path CreateScript(const std::filesystem::path& project_root,
                                   const std::string& class_name,
                                   std::string* error);

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

}  // namespace Mercury::Templates

#endif  // MERCURY_EDITOR_TEMPLATES_H
