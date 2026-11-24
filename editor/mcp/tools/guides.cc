// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The guides tool group: get_agent_guides, plus the guide and project-structure
// builders the MCP resources (resources.cc) share. The Rust side of this group
// is mercury/mcp/src/tools/guides.rs.
//
// A guide is a per-agent header (editor/mcp/guides/{agents,claude,gemini}.md)
// followed by the shared body (common.md), with the {{placeholders}} filled
// from the live editor and project. The templates are embedded at build time
// (cmake/EmbedMcpGuides.cmake); nothing is read from disk at run time.

#include "mcp/tools/guides.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <map>
#include <set>
#include <sstream>

#include "editor_scene.h"
#include "editor_settings.h"
#include "input/input_actions.h"
#include "introspection/introspection.h"
#include "mcp/mcp_host.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/guide_templates.h"
#include "project/asset_importer.h"
#include "project/project.h"

namespace Mercury::mcp {

namespace {

// ------------------------------------------------------ project structure ---

const char* const kProjectKind = "project";

std::string LowerExtension(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos ||
      (slash != std::string_view::npos && dot < slash)) {
    return {};
  }
  std::string extension(path.substr(dot));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return extension;
}

// The extensions the structure looks for: the fixed project file kinds plus
// whatever the importer registry maps to a model, texture or audio clip.
std::vector<std::string> StructureExtensions() {
  std::vector<std::string> extensions = {
      ".mproj", ".mmap", ".mprefab", ".as",    ".h",      ".cc",
      ".uxml",  ".uss",  ".minput",  ".manim", ".masset",
  };
  for (const AssetImporterInfo& info :
       AssetImporterRegistry::Instance().Importers()) {
    if (info.type != "Model" && info.type != "Texture" &&
        info.type != "AudioClip") {
      continue;
    }
    for (std::string extension : info.extensions) {
      if (extension.empty()) continue;
      if (extension[0] != '.') extension.insert(extension.begin(), '.');
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      extensions.push_back(std::move(extension));
    }
  }
  return extensions;
}

// ------------------------------------------------------------- guide text ---

// A project-supplied string as one inline code span: control characters and
// backticks removed, at most `max` bytes. Project names, tags and file names
// come from the project's files, so they must not be able to open a new line
// or close the span in the guide an agent reads.
std::string Inline(std::string_view text, size_t max = 200) {
  std::string out;
  bool cut = false;
  for (const char c : text) {
    if (out.size() >= max) {
      cut = true;
      break;
    }
    const auto byte = static_cast<unsigned char>(c);
    if (c == '`') {
      out += '\'';
    } else if (byte < 0x20 || byte == 0x7f) {
      out += ' ';
    } else {
      out += c;
    }
  }
  if (cut) {
    // Do not leave half a UTF-8 character at the cut.
    while (!out.empty() &&
           (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
      out.pop_back();
    }
    if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xC0) {
      out.pop_back();
    }
    out += "...";
  }
  return "`" + out + "`";
}

std::string JoinInline(const std::vector<std::string>& items) {
  std::string out;
  for (const std::string& item : items) {
    if (!out.empty()) out += ", ";
    out += Inline(item);
  }
  return out;
}

const char* ModeName(ToolMode mode) {
  switch (mode) {
    case ToolMode::kRead:
      return "Read";
    case ToolMode::kWrite:
      return "Write";
    case ToolMode::kExec:
      return "Exec";
  }
  return "Read";
}

// The URL clients connect to: the running server's, else what the Preferences
// MCP tab shows (the fixed port, or a placeholder for an ephemeral one).
std::string ServerUrl(EditorScene& scene) {
  if (scene.Mcp().Running()) {
    std::string url = scene.Mcp().Url();
    if (!url.empty()) return url;
  }
  const EditorSettings& settings = scene.Settings();
  if (settings.mcp_ephemeral) {
    return "http://127.0.0.1:<chosen when the server starts>/mcp";
  }
  return "http://127.0.0.1:" + std::to_string(settings.mcp_port) + "/mcp";
}

std::string EditsLine(const EditorScene& scene) {
  if (scene.Settings().mcp_allow_edits) {
    return "enabled (Access Mode: Allow Scene Edits), so Write and Exec "
           "tools work.";
  }
  return "disabled (Access Mode: Read-Only): Write and Exec tools are "
         "refused. Ask the user to switch the Access Mode in **File > "
         "Preferences > MCP** if you need to change the project.";
}

// The tool catalogue as the editor's registry holds it right now.
std::string ToolsSection() {
  struct Row {
    std::string name;
    ToolMode mode;
  };
  std::vector<Row> rows;
  for (const ToolEntry* entry : ToolRegistry::Instance().Entries()) {
    // Reserved transport names (__resources/list, ...) are not tools.
    if (entry->name.starts_with("__")) continue;
    rows.push_back({entry->name, entry->mode});
  }
  // get_server_info is answered by the transport itself and is not registered
  // in the editor (mercury/mcp/src/tools/core.rs).
  if (std::none_of(rows.begin(), rows.end(), [](const Row& row) {
        return row.name == "get_server_info";
      })) {
    rows.push_back({"get_server_info", ToolMode::kRead});
  }
  std::sort(rows.begin(), rows.end(),
            [](const Row& a, const Row& b) { return a.name < b.name; });

  std::string out = std::format(
      "{} tools. Read tools only look; Write tools change the project, the "
      "map or the editor; Exec tools run user code or processes. Each tool's "
      "description and argument schema are in the MCP tool list.\n\n"
      "| Tool | Mode |\n|---|---|\n",
      rows.size());
  for (const Row& row : rows) {
    out += std::format("| `{}` | {} |\n", row.name, ModeName(row.mode));
  }
  return out;
}

std::string InputActionTypeName(InputAction::Type type) {
  switch (type) {
    case InputAction::Type::kValue:
      return "value";
    case InputAction::Type::kPassThrough:
      return "passthrough";
    case InputAction::Type::kButton:
      break;
  }
  return "button";
}

// The input action maps and actions of the project's .minput.
void AppendInputActions(const Project& project, std::string& out) {
  const std::string path = project.GetInputActionsPath();
  if (path.empty()) {
    out +=
        "- Input actions: none (no `[input] actions` in the project file).\n";
    return;
  }
  out += "- Input actions asset: " + Inline(path) + "\n";
  std::string source;
  {
    const std::filesystem::path file = project.Resolve(path);
    std::ifstream in(file, std::ios::binary);
    if (file.empty() || !in) {
      out += "  - The file could not be read.\n";
      return;
    }
    std::ostringstream text;
    text << in.rdbuf();
    source = text.str();
  }
  InputActionsDesc desc;
  std::string error;
  if (!ParseInputActions(source, &desc, &error)) {
    out += "  - The file could not be parsed: " + Inline(error) + "\n";
    return;
  }
  out += "  - Action maps: " +
         (desc.maps.empty() ? std::string("none") : JoinInline(desc.maps)) +
         "\n";
  if (desc.actions.empty()) {
    out += "  - Actions: none\n";
    return;
  }
  out += "  - Actions (name, type):\n";
  for (const InputActionDesc& action : desc.actions) {
    out += "    - " + Inline(action.FullName()) + " (" +
           InputActionTypeName(action.type) +
           (action.vector2 ? ", vector2" : "") + ")\n";
  }
}

std::string ProjectSection(EditorScene& scene) {
  const std::shared_ptr<Project> project = scene.GetProject();
  if (!project) {
    return "No project is open in the editor.\n";
  }
  std::string out;
  out += "- Name: " + Inline(project->GetName()) + "\n";
  out += "- Project file: " + Inline(project->ToResPath(project->GetFile())) +
         "\n";
  const std::string main_map = project->GetMainMap();
  out += "- Main map: " +
         (main_map.empty() ? std::string("none set") : Inline(main_map)) + "\n";
  out += std::format("- Window: {} x {}\n", project->GetWindowWidth(),
                     project->GetWindowHeight());
  const glm::vec3 gravity = project->GetGravity();
  out += std::format(
      "- Physics: gravity ({:g}, {:g}, {:g}) units per second squared, "
      "{} fixed ticks per second\n",
      gravity.x, gravity.y, gravity.z, project->GetTickRate());
  if (!project->GetBuildTarget().empty()) {
    out += "- C++ build target: " + Inline(project->GetBuildTarget()) + "\n";
  }

  std::string layers;
  const std::vector<std::string> layer_names = project->GetLayerNames();
  for (size_t i = 0; i < layer_names.size(); i++) {
    if (layer_names[i].empty()) continue;
    if (!layers.empty()) layers += ", ";
    layers += std::to_string(i) + " " + Inline(layer_names[i]);
  }
  out += "- Layers (index name): " + (layers.empty() ? "none" : layers) + "\n";
  out += "- Tags: " + JoinInline(project->GetTags()) + "\n";
  AppendInputActions(*project, out);

  const nlohmann::json structure =
      ClassifyProjectFiles(ListStructureFiles(scene), SIZE_MAX);
  out += std::format(
      "\nSample files (up to {} per kind; the mercury://project/structure "
      "resource lists them all):\n\n",
      kSampleAssetsPerKind);
  for (const std::string_view kind : StructureKinds()) {
    const std::string key(kind);
    const nlohmann::json& files = structure["kinds"][key];
    const size_t count = structure["counts"][key].get<size_t>();
    if (count == 0) continue;
    out += "- " + key + " (" + std::to_string(count) + "): ";
    std::string names;
    for (size_t i = 0; i < files.size() && i < kSampleAssetsPerKind; i++) {
      if (!names.empty()) names += ", ";
      names += Inline(files[i].get<std::string>());
    }
    out += names;
    if (count > kSampleAssetsPerKind) out += ", ...";
    out += "\n";
  }
  return out;
}

// Replaces every {{name}} with its value in one pass. Inserted values are not
// scanned again, so project text can never expand into another placeholder; an
// unknown placeholder is left as written.
std::string Expand(std::string_view text,
                   const std::map<std::string, std::string>& values) {
  std::string out;
  size_t position = 0;
  while (position < text.size()) {
    const size_t open = text.find("{{", position);
    if (open == std::string_view::npos) break;
    const size_t close = text.find("}}", open + 2);
    if (close == std::string_view::npos) break;
    out.append(text.substr(position, open - position));
    const std::string name(text.substr(open + 2, close - open - 2));
    const auto it = values.find(name);
    if (it != values.end()) {
      out += it->second;
    } else {
      out.append(text.substr(open, close + 2 - open));
    }
    position = close + 2;
  }
  out.append(text.substr(position));
  return out;
}

// ------------------------------------------------------ get_agent_guides ---

ToolResult Error(std::string message, const std::string& suggestion = {}) {
  nlohmann::json result{{"error", std::move(message)}};
  if (!suggestion.empty()) result["suggestion"] = suggestion;
  return ToolResult{false, std::move(result), {}};
}

ToolResult GetAgentGuidesTool(EditorScene& scene, const nlohmann::json& args) {
  std::vector<std::string> names;
  const auto files = args.is_object() ? args.find("files") : args.end();
  if (args.is_object() && files != args.end() && !files->is_null()) {
    if (!files->is_array()) {
      return Error("files must be an array of guide names");
    }
    for (const nlohmann::json& item : *files) {
      if (!item.is_string()) {
        return Error("files must be an array of guide names (strings)");
      }
      const std::string name = item.get<std::string>();
      const auto& known = GuideNames();
      if (std::find(known.begin(), known.end(), name) == known.end()) {
        const auto suggestions = introspection::Suggest(name, known, 1);
        return Error("unknown guide \"" + name +
                         "\"; the guides are AGENTS.md, CLAUDE.md and "
                         "GEMINI.md",
                     suggestions.empty() ? std::string() : suggestions[0]);
      }
      if (std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
      }
    }
  }
  if (names.empty()) names = GuideNames();

  nlohmann::json list = nlohmann::json::array();
  for (const std::string& name : names) {
    list.push_back({{"name", name}, {"content", *BuildGuide(scene, name)}});
  }
  return ToolResult{true, nlohmann::json{{"files", std::move(list)}}, {}};
}

}  // namespace

// ------------------------------------------------------------- public API ---

const std::vector<std::string_view>& StructureKinds() {
  static const std::vector<std::string_view> kinds = {
      kProjectKind, "maps",        "prefabs", "scripts",  "ui",   "input",
      "animators",  "data_assets", "models",  "textures", "audio"};
  return kinds;
}

std::string StructureKindOf(std::string_view res_path) {
  const std::string extension = LowerExtension(res_path);
  if (extension.empty()) return {};
  if (extension == ".mproj") return kProjectKind;
  if (extension == ".mmap") return "maps";
  if (extension == ".mprefab") return "prefabs";
  if (extension == ".as") return "scripts";
  if ((extension == ".h" || extension == ".cc") &&
      res_path.starts_with("res://src/")) {
    return "scripts";
  }
  if (extension == ".uxml" || extension == ".uss") return "ui";
  if (extension == ".minput") return "input";
  if (extension == ".manim") return "animators";
  if (extension == ".masset") return "data_assets";
  // Find, not TypeFromExtension: that one takes a path, and ".png" alone is a
  // dot file to std::filesystem.
  const AssetImporterInfo* importer =
      AssetImporterRegistry::Instance().Find(extension);
  const std::string type = importer ? importer->type : std::string();
  if (type == "Model") return "models";
  if (type == "Texture") return "textures";
  if (type == "AudioClip") return "audio";
  return {};
}

nlohmann::json ClassifyProjectFiles(const std::vector<std::string>& res_paths,
                                    size_t cap) {
  std::map<std::string, std::vector<const std::string*>> by_kind;
  size_t total = 0;
  for (const std::string& path : res_paths) {
    std::string kind = StructureKindOf(path);
    if (kind.empty()) continue;
    by_kind[kind].push_back(&path);
    total++;
  }

  nlohmann::json kinds = nlohmann::json::object();
  nlohmann::json counts = nlohmann::json::object();
  size_t returned = 0;
  for (const std::string_view kind_view : StructureKinds()) {
    const std::string kind(kind_view);
    nlohmann::json list = nlohmann::json::array();
    const auto it = by_kind.find(kind);
    size_t count = 0;
    if (it != by_kind.end()) {
      count = it->second.size();
      for (const std::string* path : it->second) {
        if (returned >= cap) break;
        list.push_back(*path);
        returned++;
      }
    }
    kinds[kind] = std::move(list);
    counts[kind] = count;
  }
  return nlohmann::json{
      {"kinds", std::move(kinds)},
      {"counts", std::move(counts)},
      {"total", total},
      {"returned", returned},
      {"truncated", returned < total},
      {"cap", cap == SIZE_MAX ? nlohmann::json(nullptr) : nlohmann::json(cap)}};
}

std::vector<std::string> ListStructureFiles(const EditorScene& scene) {
  return scene.ListProjectFiles(StructureExtensions());
}

nlohmann::json ProjectStructureJson(const EditorScene& scene, size_t cap) {
  nlohmann::json structure =
      ClassifyProjectFiles(ListStructureFiles(scene), cap);
  if (const auto project = scene.GetProject()) {
    structure["name"] = project->GetName();
    structure["main_map"] = project->GetMainMap();
  }
  return structure;
}

const std::vector<std::string>& GuideNames() {
  static const std::vector<std::string> names = {"AGENTS.md", "CLAUDE.md",
                                                 "GEMINI.md"};
  return names;
}

std::optional<std::string> BuildGuide(EditorScene& scene,
                                      std::string_view name) {
  std::string_view header;
  if (name == "AGENTS.md") {
    header = guides::kAgents;
  } else if (name == "CLAUDE.md") {
    header = guides::kClaude;
  } else if (name == "GEMINI.md") {
    header = guides::kGemini;
  } else {
    return std::nullopt;
  }
  std::string text(header);
  text += guides::kCommon;
  return Expand(text, {{"url", ServerUrl(scene)},
                       {"edits", EditsLine(scene)},
                       {"tools", ToolsSection()},
                       {"project", ProjectSection(scene)}});
}

MERCURY_MCP_TOOL("get_agent_guides", kRead, &GetAgentGuidesTool);

}  // namespace Mercury::mcp
