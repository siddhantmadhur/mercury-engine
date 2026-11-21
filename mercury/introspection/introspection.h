// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * introspection.h
 *
 * The engine's introspection API: what the engine knows about itself, in the
 * forms tools need. The MCP server (and the editor panels that share its
 * work) consumes this; it is also usable from any process that links mercury.
 *
 * Three questions, one namespace:
 *
 *   EngineSchemaJson()  "What components, scripts and asset types exist, and
 *                        what fields do they have?" (the same data
 *                        --dump-schema writes, as JSON instead of TOML)
 *
 *   ValidateProject()   "What is wrong with this project's files?" Loads
 *                        every map, prefab, asset, animation, input and
 *                        project file through the engine's own loaders and
 *                        reports parse errors, unknown component and field
 *                        names (with did-you-mean suggestions) and res://
 *                        references to files that do not exist.
 *
 *   InspectAsset()      "What is inside this file?" Bounds and clips for a
 *                        model, dimensions for a texture, duration for an
 *                        audio clip, the fields of a .masset.
 *
 * JSON is built with nlohmann/json inside the engine's implementation files
 * (like toml++, it stays out of the public header surface), so the SDK
 * surface of this module is plain std::string results.
 */
#ifndef MERCURY_INTROSPECTION_INTROSPECTION_H
#define MERCURY_INTROSPECTION_INTROSPECTION_H

#include <string>
#include <string_view>
#include <vector>

#include "project/project.h"

namespace Mercury::introspection {

/**
 * @brief Every component, script and asset type the registry knows about
 *
 * Covers all TypeRegistry entries (built-ins, native scripts, schema
 * scripts and data assets) with their fields. Each field entry carries:
 *
 *   name          the field name
 *   type          "bool", "int", "float", "string", "Vector2", "Vector3",
 *                 "Color", "NodePath", "Resource", "enum", "list", "event"
 *   default       the field's default value, in JSON form
 *                 ({ "vec3": [x, y, z] }, { "res": "res://..." }, ...)
 *   enum_values   the allowed strings for an "enum" field ([] otherwise)
 *   asset_type    the expected asset type for a "Resource" field ("" if any)
 *   tooltip       the editor tooltip, "" when there is none
 *   element       for a "list" field, the element's type name
 *   runtime_writable  whether scripts may write it at run time
 *
 * Components additionally carry category, description, is_script and
 * removable; asset types carry name, description and their fields. Built-in
 * types are registered on demand, so this works in any process.
 */
std::string EngineSchemaJson();

/**
 * @brief One problem found in a project's files
 *
 * `file` is the res:// path ("res://maps/main.mmap"), or "" for a
 * project-level note. `line` is 1-based when the loader or parser gave one,
 * 0 when it did not. `severity` is:
 *
 *   "error"   the project will not load, a referenced file is missing, or a
 *             field name or enum value the registry does not know — those
 *             are typos the loader silently drops, so they must be seen;
 *   "warning" a component or asset type name the registry does not know:
 *             it may still be a script whose schema has not been loaded.
 *
 * `suggestion` is a did-you-mean value from @ref Suggest, "" when none.
 */
struct Diagnostic {
  std::string file;
  int line = 0;
  std::string severity;
  std::string message;
  std::string suggestion;
};

/**
 * @brief Everything @ref ValidateProject found in a project
 */
struct ValidationReport {
  std::vector<Diagnostic> diagnostics;

  // True when any diagnostic has severity "error".
  bool HasErrors() const;
  // {"has_errors": ..., "diagnostics": [{"file", "line", "severity",
  //  "message", "suggestion"}, ...]}
  std::string ToJson() const;
  // "file:line: severity: message" lines, with " (did you mean "x"?)" for
  // suggestions; line 0 and an empty file are omitted.
  std::string ToText() const;
};

/**
 * @brief Load every project file through the engine's own loaders and report
 *        what is wrong
 *
 * Walks the project folder for .mproj, .mmap, .mprefab, .masset, .manim and
 * .minput files. For each one it:
 *
 *   - parses it (mtext) and runs the existing loader (Map, AssetDatabase,
 *     AnimatorController, InputActions, Project), reporting loader and parse
 *     errors as "error" diagnostics with the file and, where the loader
 *     gives one, the line;
 *   - checks component and asset type names and every field name against the
 *     TypeRegistry with a did-you-mean suggestion: an unknown component or
 *     asset type name is a "warning" (it may be a script whose schema has
 *     not been loaded), while an unknown field or enum value on a
 *     registered type is an "error", because load silently drops it;
 *   - checks that every res:// reference (ext_resource entries, Resource
 *     values, .minput headers, .manim models, main_map, the input actions
 *     asset) points at a file that exists, as an "error" with a suggestion
 *     when a project file with a similar name exists.
 *
 * In a process whose registry has no script types yet (a standalone CLI),
 * the project's .mercury/scripts.mschema, if present, is loaded first so
 * the project's own scripts are known; when script types are already
 * registered (an editor or game process) the live registry is used as is
 * and never modified.
 */
ValidationReport ValidateProject(const Project& project);

/**
 * @brief What is inside one asset file, as JSON
 *
 * The shape depends on the type:
 *
 *   Model       bounds (min/max), every node (name, parent index, position,
 *               rotation quaternion, scale), every animation clip (name,
 *               duration) and a note that pivots are flattened at import.
 *   Texture     width, height, pixel format and whether it is sRGB.
 *   AudioClip   duration (when the decoder reports it), channels, sample
 *               rate.
 *   .masset     the header's type, the file's property values, and — when
 *               the type is registered — the registry's field descriptions
 *               with which of them the file sets.
 *   other       the asset type from its extension.
 *
 * @return true and fills `json_out` on success; false and fills `error` when
 *         the path is not res://, escapes the project, or the file is
 *         missing or cannot be read.
 */
bool InspectAsset(const Project& project, std::string_view res_path,
                  std::string* json_out, std::string* error);

/**
 * @brief The closest candidates to a mistyped name (did you mean ...?)
 *
 * Case-insensitive Damerau-Levenshtein distance; returns up to `max`
 * candidates ordered by distance (ties alphabetical), empty when nothing is
 * within a small edit budget, so unrelated words never get a suggestion.
 */
std::vector<std::string> Suggest(std::string_view word,
                                 const std::vector<std::string>& candidates,
                                 int max = 3);

}  // namespace Mercury::introspection

#endif  // MERCURY_INTROSPECTION_INTROSPECTION_H
