// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * guides.h
 *
 * The agent guides and the project structure, shared by the get_agent_guides
 * tool (guides.cc) and the MCP resources (resources.cc).
 *
 * INTERNAL header (it includes nlohmann/json): not installed in the SDK, like
 * everything under mcp/tools.
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_GUIDES_H
#define MERCURY_EDITOR_MCP_TOOLS_GUIDES_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Mercury {

class EditorScene;

}  // namespace Mercury

namespace Mercury::mcp {

// The most files mercury://project/structure lists.
constexpr size_t kStructureEntryCap = 2000;
// How many files of each kind the guides name as samples.
constexpr size_t kSampleAssetsPerKind = 20;

// The kinds the project structure groups files into, in listing order:
// project, maps, prefabs, scripts, ui, input, animators, data_assets, models,
// textures, audio.
const std::vector<std::string_view>& StructureKinds();

// The kind of one res:// path ("" when it is none of them): by extension, so
// ".as" is a script, and ".h" / ".cc" are scripts only under res://src/.
// Models, textures and audio clips are what the AssetImporterRegistry says.
std::string StructureKindOf(std::string_view res_path);

/**
 * @brief Groups res:// paths by kind
 *
 * {"kinds": {kind: [path, ...]}, "counts": {kind: n}, "total": n,
 *  "returned": n, "truncated": bool, "cap": n}
 *
 * Every kind is present (empty when it has no file). Paths keep the input
 * order within a kind; paths of no kind are dropped. At most `cap` paths are
 * listed in all, filled kind by kind in StructureKinds() order, so a project
 * with thousands of textures still shows its maps and scripts; `truncated`
 * says some were left out and `counts` always has the true size of each kind.
 */
nlohmann::json ClassifyProjectFiles(const std::vector<std::string>& res_paths,
                                    size_t cap = kStructureEntryCap);

// The open project's files with a structure kind (sorted res:// paths); empty
// with no project.
std::vector<std::string> ListStructureFiles(const EditorScene& scene);

// mercury://project/structure: ClassifyProjectFiles of the open project plus
// its name and main map. {"error": ...} content when no project is open is the
// caller's job (see resources.cc): this returns an object with empty kinds.
nlohmann::json ProjectStructureJson(const EditorScene& scene,
                                    size_t cap = kStructureEntryCap);

// "AGENTS.md", "CLAUDE.md", "GEMINI.md".
const std::vector<std::string>& GuideNames();

// The guide `name` rendered for the open project, nullopt for an unknown name.
std::optional<std::string> BuildGuide(EditorScene& scene,
                                      std::string_view name);

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOLS_GUIDES_H
