// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The introspection API's implementation. One translation unit on purpose:
// nlohmann/json stays inside it (like toml++, a private dependency), so the
// public header surface is unchanged. Reuses the existing loaders —
// Map, AssetDatabase, AnimatorController, InputActions, Project, Model —
// instead of re-parsing anything they already understand.

#include "introspection/introspection.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <utility>

#include <miniaudio/miniaudio.h>
#include <nlohmann/json.hpp>

#include "animation/animator_controller.h"
#include "input/input_actions.h"
#include "os/filesystem.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/prefab_cache.h"
#include "properties/model.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"
#include "serialization/mtext.h"

namespace Mercury::introspection {

namespace {

namespace fs = std::filesystem;

// ------------------------------------------------------------- JSON helpers --

// A Variant value in JSON form, mirroring the engine's text format tags:
// { vec2 = [x, y] } -> {"vec2": [x, y]}, { res = "..." } -> {"res": "..."},
// { nil = true } -> null.
nlohmann::json VariantToJson(const mtext::Variant& value) {
  using Type = mtext::Variant::Type;
  switch (value.GetType()) {
    case Type::kNil:
      return nullptr;
    case Type::kBool:
      return *value.Get<bool>();
    case Type::kInt:
      return *value.Get<int64_t>();
    case Type::kFloat:
      return *value.Get<double>();
    case Type::kString:
      return *value.Get<std::string>();
    case Type::kVector2: {
      const glm::vec2 v = *value.Get<glm::vec2>();
      return nlohmann::json::array({v.x, v.y});
    }
    case Type::kVector3: {
      const glm::vec3 v = *value.Get<glm::vec3>();
      return nlohmann::json::array({v.x, v.y, v.z});
    }
    case Type::kColor: {
      const glm::vec4 v = value.Get<mtext::Color>()->rgba;
      return nlohmann::json::array({v.r, v.g, v.b, v.a});
    }
    case Type::kNodePath:
      return nlohmann::json{{"path", value.Get<mtext::NodePath>()->path}};
    case Type::kExtResource:
      return nlohmann::json{{"ext", value.Get<mtext::ExtResource>()->id}};
    case Type::kResource:
      return nlohmann::json{{"res", value.Get<mtext::Resource>()->path}};
    case Type::kArray: {
      nlohmann::json array = nlohmann::json::array();
      for (const auto& item : *value.Get<mtext::Array>()) {
        array.push_back(VariantToJson(item));
      }
      return array;
    }
  }
  return nullptr;
}

nlohmann::json FieldsJson(const std::vector<FieldInfo>& fields) {
  nlohmann::json out = nlohmann::json::array();
  for (const FieldInfo& field : fields) {
    nlohmann::json entry;
    entry["name"] = field.name;
    entry["type"] = FieldTypeName(field.type);
    entry["default"] = VariantToJson(field.default_value);
    entry["enum_values"] = field.enum_values;
    entry["asset_type"] = field.asset_type;
    entry["tooltip"] = field.tooltip;
    entry["element"] =
        field.type == FieldType::kList ? FieldTypeName(field.element_type) : "";
    entry["runtime_writable"] = field.runtime_writable;
    out.push_back(std::move(entry));
  }
  return out;
}

// --------------------------------------------------------------- Suggesting --

std::string ToLower(std::string_view text) {
  std::string out(text);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// Restricted Damerau-Levenshtein (adjacent transpositions, no substring
// edits), case-insensitive: the usual did-you-mean metric.
size_t EditDistance(std::string_view a, std::string_view b) {
  const std::string x = ToLower(a);
  const std::string y = ToLower(b);
  const size_t n = x.size();
  const size_t m = y.size();
  if (n == 0) return m;
  if (m == 0) return n;
  // Three rolling rows of the (n+1) x (m+1) dynamic program.
  std::vector<size_t> prev2(m + 1, 0), prev(m + 1, 0), cur(m + 1, 0);
  for (size_t j = 0; j <= m; j++) prev[j] = j;
  for (size_t i = 1; i <= n; i++) {
    cur[0] = i;
    for (size_t j = 1; j <= m; j++) {
      const size_t substitution = prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1);
      size_t best = std::min({substitution, prev[j] + 1, cur[j - 1] + 1});
      if (i > 1 && j > 1 && x[i - 1] == y[j - 2] && x[i - 2] == y[j - 1]) {
        best = std::min(best, prev2[j - 2] + 1);
      }
      cur[j] = best;
    }
    prev2 = prev;
    prev = cur;
  }
  return prev[m];
}

// The furthest distance a mistyped word may be from a suggestion: anything
// further reads as an unrelated word. Longer words get more room, very short
// ones only a single edit so one-letter differences stay informative.
size_t SuggestLimit(size_t word_length) {
  if (word_length <= 3) return 1;
  return std::max<size_t>(2, word_length / 3);
}

// --------------------------------------------------------------- Diagnostics
// --

void AddDiagnostic(ValidationReport& report, const char* severity,
                   std::string file, int line, std::string message,
                   std::string suggestion = "") {
  report.diagnostics.push_back({std::move(file), line, severity,
                                std::move(message), std::move(suggestion)});
}

// A loader error string ("file:line:col: message", "file:line: message" or
// "file: message"): the line number, and the message without the prefix.
std::pair<int, std::string> SplitLoaderError(const fs::path& file,
                                             const std::string& message) {
  const std::string prefix = file.string();
  std::string_view rest = message;
  if (!rest.starts_with(prefix)) return {0, message};
  rest.remove_prefix(prefix.size());
  if (rest.starts_with(':')) rest.remove_prefix(1);
  while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
  const auto digits = [](std::string_view text) {
    size_t i = 0;
    while (i < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[i]))) {
      i++;
    }
    return i;
  };
  const size_t line_end = digits(rest);
  if (line_end == 0 || line_end >= rest.size() || rest[line_end] != ':') {
    return {0, std::string(rest)};  // no line: the message stands alone
  }
  const int line = std::atoi(std::string(rest.substr(0, line_end)).c_str());
  rest.remove_prefix(line_end + 1);
  // A ":column" follows the line when it came from the parser.
  const size_t column_end = digits(rest);
  if (column_end > 0 && column_end < rest.size() && rest[column_end] == ':') {
    rest.remove_prefix(column_end + 1);
  }
  while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
  return {line, std::string(rest)};
}

// True when a res:// path points at a file that exists (packed files count).
bool ResFileExists(const Project& project, std::string_view path) {
  if (!IsResPath(path)) return true;  // not a project reference
  const fs::path file = project.Resolve(path);
  if (file.empty()) return false;  // escapes the project root
  if (os::IsPacked(file)) return true;
  std::error_code ec;
  return fs::exists(file, ec);
}

// The project file whose name is closest to a missing reference's file name,
// "" when none is close.
std::string SuggestResPath(const std::string& res_path,
                           const std::vector<std::string>& all_files) {
  const std::string name =
      ToLower(res_path.substr(res_path.find_last_of('/') + 1));
  if (name.empty()) return "";
  const size_t limit = SuggestLimit(name.size());
  std::string best;
  size_t best_distance = limit + 1;
  for (const std::string& candidate : all_files) {
    const std::string candidate_name =
        ToLower(candidate.substr(candidate.find_last_of('/') + 1));
    if (candidate_name == name) continue;  // would be the same file
    const size_t distance = EditDistance(name, candidate_name);
    if (distance < best_distance) {
      best_distance = distance;
      best = candidate;
    }
  }
  return best;
}

void CheckResRef(ValidationReport& report, const Project& project,
                 const std::string& file, int line, std::string_view path,
                 const std::vector<std::string>& all_files) {
  if (!IsResPath(path) || ResFileExists(project, path)) return;
  AddDiagnostic(report, "error", file, line,
                std::format("referenced file {} does not exist", path),
                SuggestResPath(std::string(path), all_files));
}

// Applies CheckResRef to every Resource value in a property list, descending
// into arrays (ExtResource references are checked by the loaders themselves;
// their target paths are checked at their [ext_resource] entry).
void CheckResourceValues(ValidationReport& report, const Project& project,
                         const std::string& file, int line,
                         const mtext::PropertyList& properties,
                         const std::vector<std::string>& all_files) {
  constexpr int kMaxValueDepth = 128;  // matches the parser's cap
  std::function<void(const mtext::Variant&, int)> visit =
      [&](const mtext::Variant& value, int depth) {
        if (auto array = value.Get<mtext::Array>();
            array && depth < kMaxValueDepth) {
          for (const auto& item : *array) visit(item, depth + 1);
          return;
        }
        if (auto resource = value.Get<mtext::Resource>()) {
          CheckResRef(report, project, file, line, resource->path, all_files);
        }
      };
  for (const auto& property : properties) visit(property.value, 0);
}

// A field's value that must be one of an enum field's strings.
void CheckEnumValue(const FieldInfo& field, const mtext::Variant& value,
                    ValidationReport& report, const std::string& file,
                    int line) {
  if (field.type != FieldType::kEnum) return;
  const auto text = value.ToString();
  if (!text) return;  // an unset value ({ nil = true }) is fine
  if (std::find(field.enum_values.begin(), field.enum_values.end(), *text) !=
      field.enum_values.end()) {
    return;
  }
  const std::string allowed = std::accumulate(
      field.enum_values.begin(), field.enum_values.end(), std::string(),
      [](const std::string& a, const std::string& b) -> std::string {
        return a.empty() ? b : a + ", " + b;
      });
  const auto suggestions = Suggest(*text, field.enum_values, 1);
  AddDiagnostic(report, "error", file, line,
                std::format("value \"{}\" is not one of the {} enum's values "
                            "({})",
                            *text, field.name, allowed),
                suggestions.empty() ? "" : suggestions.front());
}

std::vector<std::string> FieldNames(const std::vector<FieldInfo>& fields) {
  std::vector<std::string> names;
  names.reserve(fields.size());
  for (const auto& field : fields) names.push_back(field.name);
  return names;
}

// Checks a component's property list against the component's registered
// fields; used by maps, prefabs and massets alike. The type is known here,
// so a name the registry does not know is a typo that load silently drops.
void CheckFields(ValidationReport& report, const std::string& file, int line,
                 std::string_view context,
                 const mtext::PropertyList& properties,
                 const std::vector<FieldInfo>& fields,
                 std::string_view what_for_message) {
  const std::vector<std::string> names = FieldNames(fields);
  for (const auto& property : properties) {
    auto field = std::find_if(
        fields.begin(), fields.end(),
        [&](const FieldInfo& f) { return f.name == property.key; });
    if (field != fields.end()) {
      CheckEnumValue(*field, property.value, report, file, line);
      continue;
    }
    std::vector<std::string> suggestions = Suggest(property.key, names, 1);
    AddDiagnostic(report, "error", file, line,
                  std::format("unknown {} field \"{}\" of {}", what_for_message,
                              property.key, context),
                  suggestions.empty() ? "" : suggestions.front());
  }
}

// --------------------------------------------------------------- File walks --

// Every file under the project root, skipping generated and hidden folders
// (.mercury, .git): the ones with a loadable extension in `project_files`,
// everything in `all_files` (candidates for did-you-mean on res paths).
void CollectFiles(const fs::path& root, std::vector<fs::path>& project_files,
                  std::vector<std::string>& all_files) {
  std::error_code ec;
  for (fs::recursive_directory_iterator
           it(root, fs::directory_options::skip_permission_denied, ec),
       end;
       it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const fs::directory_entry& entry = *it;
    const std::string name = entry.path().filename().string();
    if (entry.is_directory(ec) && !ec &&
        (name.empty() || name.front() == '.')) {
      it.disable_recursion_pending();
      continue;
    }
    ec.clear();
    std::error_code file_ec;
    if (!entry.is_regular_file(file_ec) || file_ec) continue;
    if (entry.is_symlink(file_ec) || file_ec) continue;
    const std::string res_path = it->path().generic_string();
    all_files.push_back(res_path);
    const std::string extension = entry.path().extension().string();
    if (extension == kProjectExtension || extension == kMapExtension ||
        extension == ".mprefab" || extension == kAssetExtension ||
        extension == ".manim" || extension == kInputActionsExtension) {
      project_files.push_back(entry.path());
    }
  }
  std::sort(project_files.begin(), project_files.end());
}

// ------------------------------------------------------------- Per-kind checks
// --

void ValidateMapFile(ValidationReport& report, const Project& project,
                     const std::string& res_path,
                     const mtext::Document& document,
                     const TypeRegistry& registry,
                     const std::vector<std::string>& component_names,
                     const std::vector<std::string>& all_files) {
  // Declared external resources: their target must exist.
  std::map<std::string, std::string> ext_paths;  // id -> res path
  for (const auto& section : document.sections) {
    if (section.tag != "ext_resource") continue;
    const std::string path = section.attributes.GetString("path");
    ext_paths[section.attributes.GetString("id")] = path;
    if (!path.empty()) {
      CheckResRef(report, project, res_path, section.line, path, all_files);
    }
  }
  // Components against the registry. A type or field that appears on many
  // nodes is reported once per file, not once per section.
  std::set<std::string> reported_types;
  std::set<std::string> reported_fields;
  for (const auto& section : document.sections) {
    if (section.tag != "component") continue;
    const std::string type = section.attributes.GetString("type");
    if (type.empty()) continue;  // the loader reports "needs a type"
    const ComponentInfo* info = registry.FindComponent(type);
    if (!info) {
      if (reported_types.insert(type).second) {
        std::vector<std::string> suggestions =
            Suggest(type, component_names, 1);
        AddDiagnostic(report, "warning", res_path, section.line,
                      std::format("component type \"{}\" is not registered "
                                  "(is its script's schema loaded?)",
                                  type),
                      suggestions.empty() ? "" : suggestions.front());
      }
      continue;
    }
    for (const auto& property : section.properties) {
      const FieldInfo* field = info->FindField(property.key);
      if (field) {
        CheckEnumValue(*field, property.value, report, res_path, section.line);
        continue;
      }
      if (!reported_fields.insert(type + "/" + property.key).second) continue;
      std::vector<std::string> suggestions =
          Suggest(property.key, FieldNames(info->fields), 1);
      AddDiagnostic(report, "error", res_path, section.line,
                    std::format("unknown property field \"{}\" of component {}",
                                property.key, type),
                    suggestions.empty() ? "" : suggestions.front());
    }
  }
  // res:// values anywhere else (node instances, plain resource fields).
  for (const auto& section : document.sections) {
    if (section.tag == "ext_resource") continue;
    CheckResourceValues(report, project, res_path, section.line,
                        section.attributes, all_files);
    CheckResourceValues(report, project, res_path, section.line,
                        section.properties, all_files);
  }
}

void ValidateAssetFile(ValidationReport& report, const Project& project,
                       const std::string& res_path,
                       const mtext::Document& document,
                       const TypeRegistry& registry,
                       const std::vector<std::string>& asset_type_names,
                       const std::vector<std::string>& all_files) {
  Asset asset;
  std::string error;
  if (!LoadAssetFile(project.Resolve(res_path), asset, &error)) {
    const auto [line, message] =
        SplitLoaderError(project.Resolve(res_path), error);
    AddDiagnostic(report, "error", res_path, line, message);
    return;
  }
  const AssetTypeInfo* info = registry.FindAssetType(asset.type);
  if (asset.type.empty()) {
    AddDiagnostic(report, "error", res_path, 1,
                  "the [masset] header has no type");
    return;
  }
  if (!info) {
    std::vector<std::string> suggestions =
        Suggest(asset.type, asset_type_names, 1);
    AddDiagnostic(
        report, "warning", res_path, 1,
        std::format("asset type \"{}\" is not registered", asset.type),
        suggestions.empty() ? "" : suggestions.front());
    CheckResourceValues(report, project, res_path, 0, asset.properties,
                        all_files);
    return;
  }
  CheckFields(report, res_path, 1, std::format("asset {}", asset.type),
              asset.properties, info->fields, "field");
  CheckResourceValues(report, project, res_path, 0, asset.properties,
                      all_files);
  (void)document;
}

void ValidateAnimatorFile(ValidationReport& report, const Project& project,
                          const std::string& res_path,
                          const mtext::Document& document,
                          const std::vector<std::string>& all_files) {
  // The parameters [[parameter]] sections declare; states and transitions
  // may only name those.
  std::vector<std::string> parameters;
  for (const auto& section : document.sections) {
    if (section.tag != "parameter") continue;
    parameters.push_back(section.attributes.GetString("name"));
  }
  auto check_parameter = [&](const std::string& name, int line) {
    if (name.empty() || std::find(parameters.begin(), parameters.end(), name) !=
                            parameters.end()) {
      return;
    }
    std::vector<std::string> suggestions = Suggest(name, parameters, 1);
    AddDiagnostic(
        report, "warning", res_path, line,
        std::format("animator parameter \"{}\" is not declared", name),
        suggestions.empty() ? "" : suggestions.front());
  };
  for (const auto& section : document.sections) {
    if (section.tag == "model") {
      if (auto model = section.properties.Get("model")) {
        if (auto resource = model->Get<mtext::Resource>()) {
          CheckResRef(report, project, res_path, section.line, resource->path,
                      all_files);
        } else if (auto text = model->ToString()) {
          CheckResRef(report, project, res_path, section.line, *text,
                      all_files);
        }
      }
    } else if (section.tag == "state") {
      check_parameter(section.properties.GetString("speed_parameter"),
                      section.line);
      check_parameter(section.properties.GetString("parameter"), section.line);
      check_parameter(section.properties.GetString("parameter_y"),
                      section.line);
    } else if (section.tag == "transition") {
      if (auto conditions = section.properties.Get("conditions")) {
        if (auto array = conditions->Get<mtext::Array>()) {
          for (const auto& item : *array) {
            auto parts = item.Get<mtext::Array>();
            if (!parts || parts->empty()) continue;
            if (auto name = (*parts)[0].ToString()) {
              check_parameter(*name, section.line);
            }
          }
        }
      }
    }
  }
}

void ValidateInputActionsFile(ValidationReport& report, const Project& project,
                              const std::string& res_path,
                              const mtext::Document& document,
                              const std::vector<std::string>& all_files) {
  for (const auto& section : document.sections) {
    if (section.tag != "minput") continue;
    const std::string header = section.attributes.GetString("header");
    if (!header.empty()) {
      CheckResRef(report, project, res_path, section.line, header, all_files);
    }
  }
  std::set<std::string> names;
  for (const auto& section : document.sections) {
    if (section.tag != "action") continue;
    const std::string name = section.attributes.GetString("name");
    if (name.empty()) {
      AddDiagnostic(report, "warning", res_path, section.line,
                    "action has no name");
      continue;
    }
    if (!names.insert(name).second) {
      AddDiagnostic(report, "warning", res_path, section.line,
                    std::format("duplicate action \"{}\"", name));
    }
  }
}

void ValidateProjectFile(ValidationReport& report, const Project& project,
                         const std::string& res_path,
                         const mtext::Document& document,
                         const std::vector<std::string>& all_files) {
  // Settings live in [tag.props] since format 2; older files kept them in
  // the header's attributes.
  auto setting = [](const mtext::Section& section,
                    std::string_view key) -> std::string {
    if (auto value = section.properties.Get(key)) {
      if (auto text = value->ToString()) return *text;
    }
    return section.attributes.GetString(key);
  };
  for (const auto& section : document.sections) {
    if (section.tag == "project") {
      CheckResRef(report, project, res_path, section.line,
                  setting(section, "main_map"), all_files);
    } else if (section.tag == "input") {
      CheckResRef(report, project, res_path, section.line,
                  setting(section, "actions"), all_files);
    }
  }
}

}  // namespace

// ------------------------------------------------------------------ Suggest --

std::vector<std::string> Suggest(std::string_view word,
                                 const std::vector<std::string>& candidates,
                                 int max) {
  if (max <= 0 || word.empty()) return {};
  struct Match {
    std::string name;
    size_t distance;
  };
  std::vector<Match> matches;
  const size_t limit = SuggestLimit(word.size());
  for (const std::string& candidate : candidates) {
    if (candidate.empty()) continue;
    const size_t distance = EditDistance(word, candidate);
    if (distance <= limit) matches.push_back({candidate, distance});
  }
  std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
    if (a.distance != b.distance) return a.distance < b.distance;
    return a.name < b.name;
  });
  std::vector<std::string> out;
  out.reserve(std::min<size_t>(matches.size(), static_cast<size_t>(max)));
  for (size_t i = 0; i < matches.size() && static_cast<int>(out.size()) < max;
       i++) {
    out.push_back(std::move(matches[i].name));
  }
  return out;
}

// ---------------------------------------------------------- ValidationReport
// --

bool ValidationReport::HasErrors() const {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == "error";
                     });
}

std::string ValidationReport::ToJson() const {
  nlohmann::json out;
  out["has_errors"] = HasErrors();
  nlohmann::json entries = nlohmann::json::array();
  for (const Diagnostic& diagnostic : diagnostics) {
    entries.push_back({
        {"file", diagnostic.file},
        {"line", diagnostic.line},
        {"severity", diagnostic.severity},
        {"message", diagnostic.message},
        {"suggestion", diagnostic.suggestion},
    });
  }
  out["diagnostics"] = std::move(entries);
  return out.dump(2);
}

std::string ValidationReport::ToText() const {
  std::string out;
  for (const Diagnostic& diagnostic : diagnostics) {
    std::string prefix = diagnostic.file;
    if (diagnostic.line > 0) {
      prefix += std::format(":{}", diagnostic.line);
    }
    if (!prefix.empty()) prefix += ": ";
    out += std::format("{}{}: {}\n", prefix, diagnostic.severity,
                       diagnostic.message);
    if (!diagnostic.suggestion.empty()) {
      out += std::format("  Did you mean \"{}\"?\n", diagnostic.suggestion);
    }
  }
  return out;
}

// ------------------------------------------------------------- ValidateProject
// --

// True when script or game-registered types are in the registry: an editor
// or game process. Their script list is theirs to manage, so ValidateProject
// must not touch the live registry.
bool RegistryKnowsScripts(const TypeRegistry& registry) {
  for (const ComponentInfo* component : registry.Components()) {
    if (component->is_script || component->from_game ||
        component->native_script) {
      return true;
    }
  }
  for (const AssetTypeInfo* asset_type : registry.AssetTypes()) {
    if (asset_type->from_game || asset_type->native_asset) return true;
  }
  return false;
}

ValidationReport ValidateProject(const Project& project) {
  ValidationReport report;
  const fs::path root = project.GetRoot();
  RegisterBuiltinTypes();
  const TypeRegistry& registry = TypeRegistry::Instance();

  // The project's own scripts, when something has played it before: without
  // them every script component would read as unknown. Only in a process
  // with no script types yet (a standalone CLI), where LoadSchema cannot
  // clobber entries the caller is using.
  if (!RegistryKnowsScripts(registry)) {
    const fs::path schema = root / kProjectCacheDir / "scripts.mschema";
    if (fs::exists(schema)) {
      std::string error;
      if (!TypeRegistry::Instance().LoadSchema(schema, &error)) {
        AddDiagnostic(report, "warning", project.ToResPath(schema), 0,
                      std::format("could not load the project's script "
                                  "schema: {}",
                                  error));
      }
    }
  }

  std::vector<std::string> component_names;
  std::vector<std::string> asset_type_names;
  for (const ComponentInfo* component : registry.Components()) {
    component_names.push_back(component->name);
  }
  for (const AssetTypeInfo* asset_type : registry.AssetTypes()) {
    asset_type_names.push_back(asset_type->name);
  }

  std::vector<fs::path> project_files;
  std::vector<std::string> all_files;
  CollectFiles(root, project_files, all_files);
  // res paths, not filesystem paths, are what references use.
  for (std::string& file : all_files) {
    const fs::path relative(file);
    file = "res://" + relative.lexically_relative(root).generic_string();
  }

  for (const fs::path& file : project_files) {
    const std::string res_path = project.ToResPath(file);
    const std::string extension = file.extension().string();

    auto text = os::ReadText(file);
    if (!text) {
      AddDiagnostic(report, "error", res_path, 0,
                    std::format("could not read {}", res_path));
      continue;
    }
    mtext::Document document;
    mtext::ParseError parse_error;
    if (!mtext::Parse(*text, document, &parse_error)) {
      AddDiagnostic(report, "error", res_path, parse_error.line,
                    parse_error.message);
      continue;  // the loader would fail on the same parse
    }

    if (extension == kMapExtension || extension == ".mprefab") {
      ValidateMapFile(report, project, res_path, document, registry,
                      component_names, all_files);
      // The loader's own checks: parents before children, missing nodes,
      // undeclared ext_resources, prefab expansion, format headers.
      std::shared_ptr<const Project> shared(&project, [](const Project*) {});
      PrefabCache cache(shared);
      Map map;
      std::string error;
      if (!map.Load(file, &error, cache.Loader())) {
        const auto [line, message] = SplitLoaderError(file, error);
        AddDiagnostic(report, "error", res_path, line, message);
      }
    } else if (extension == kAssetExtension) {
      ValidateAssetFile(report, project, res_path, document, registry,
                        asset_type_names, all_files);
    } else if (extension == ".manim") {
      ValidateAnimatorFile(report, project, res_path, document, all_files);
      std::string error;
      if (!AnimatorController::Load(file.string(), &error)) {
        const auto [line, message] = SplitLoaderError(file, error);
        AddDiagnostic(report, "error", res_path, line, message);
      }
    } else if (extension == kInputActionsExtension) {
      ValidateInputActionsFile(report, project, res_path, document, all_files);
      InputActionAsset actions;
      std::string error;
      if (!actions.Load(file, &error)) {
        const auto [line, message] = SplitLoaderError(file, error);
        AddDiagnostic(report, "error", res_path, line, message);
      }
    } else if (extension == kProjectExtension) {
      if (file == project.GetFile()) {
        // Already loaded by the caller; check the settings it references.
        ValidateProjectFile(report, project, res_path, document, all_files);
      } else {
        ValidateProjectFile(report, project, res_path, document, all_files);
        std::string error;
        if (!Project::Load(file, &error)) {
          const auto [line, message] = SplitLoaderError(file, error);
          AddDiagnostic(report, "error", res_path, line, message);
        }
      }
    }
  }
  return report;
}

// -------------------------------------------------------------
// EngineSchemaJson --

std::string EngineSchemaJson() {
  RegisterBuiltinTypes();
  const TypeRegistry& registry = TypeRegistry::Instance();

  nlohmann::json out;
  nlohmann::json components = nlohmann::json::array();
  for (const ComponentInfo* component : registry.Components()) {
    nlohmann::json entry;
    entry["name"] = component->name;
    entry["category"] = component->category;
    entry["description"] = component->description;
    entry["is_script"] = component->is_script;
    entry["removable"] = component->removable;
    entry["fields"] = FieldsJson(component->fields);
    components.push_back(std::move(entry));
  }
  out["components"] = std::move(components);

  nlohmann::json asset_types = nlohmann::json::array();
  for (const AssetTypeInfo* asset_type : registry.AssetTypes()) {
    nlohmann::json entry;
    entry["name"] = asset_type->name;
    entry["description"] = asset_type->description;
    entry["fields"] = FieldsJson(asset_type->fields);
    asset_types.push_back(std::move(entry));
  }
  out["asset_types"] = std::move(asset_types);
  return out.dump(2);
}

// --------------------------------------------------------------- InspectAsset
// --

namespace {

// Models, textures and audio clips: what the import keeps, without owning a
// renderer or an audio device.
bool InspectModel(const fs::path& file, const std::string& res_path,
                  std::string* json_out, std::string* error) {
  const ModelImportSettings settings =
      ModelImportSettings::ForFile(file.string(), ModelImportSettings{});
  std::string load_error;
  auto model = Model::Load(file.string(), settings, &load_error);
  if (!model) {
    if (error) {
      *error = std::format("could not load model {}: {}", res_path,
                           load_error.empty() ? "import failed" : load_error);
    }
    return false;
  }
  nlohmann::json out;
  out["path"] = res_path;
  out["type"] = "Model";
  const glm::vec3 min = model->BoundsMin();
  const glm::vec3 max = model->BoundsMax();
  out["bounds"] = {
      {"min", nlohmann::json::array({min.x, min.y, min.z})},
      {"max", nlohmann::json::array({max.x, max.y, max.z})},
  };
  nlohmann::json nodes = nlohmann::json::array();
  for (const ModelNode& node : model->Nodes()) {
    nlohmann::json entry;
    entry["name"] = node.name;
    entry["parent"] = node.parent;
    const glm::quat r = node.rotation;
    entry["position"] = nlohmann::json::array(
        {node.position.x, node.position.y, node.position.z});
    entry["rotation"] = nlohmann::json::array({r.x, r.y, r.z, r.w});
    entry["scale"] =
        nlohmann::json::array({node.scale.x, node.scale.y, node.scale.z});
    nodes.push_back(std::move(entry));
  }
  out["nodes"] = std::move(nodes);
  nlohmann::json clips = nlohmann::json::array();
  for (const AnimationClip& clip : model->Clips()) {
    clips.push_back({{"name", clip.name}, {"duration", clip.duration}});
  }
  out["clips"] = std::move(clips);
  out["note"] =
      "Pivots are flattened at import: the source file's pivot offsets are "
      "baked into each node's transform, so nodes report the transforms the "
      "engine actually draws with.";
  if (json_out) *json_out = out.dump(2);
  return true;
}

bool InspectTexture(const fs::path& file, const std::string& res_path,
                    std::string* json_out, std::string* error) {
  auto image = ImageData::Load(file.string());
  if (!image) {
    if (error) {
      *error = std::format("could not decode texture {}", res_path);
    }
    return false;
  }
  // ImageData decodes to RGBA8 whatever the file's own compression is.
  nlohmann::json out;
  out["path"] = res_path;
  out["type"] = "Texture";
  out["width"] = image->width;
  out["height"] = image->height;
  out["format"] = "RGBA8";
  out["srgb"] = image->srgb;
  if (json_out) *json_out = out.dump(2);
  return true;
}

bool InspectAudioClip(const fs::path& file, const std::string& res_path,
                      std::string* json_out, std::string* error) {
  // A decoder without an audio device: miniaudio reads the file's headers
  // only, which keeps an MCP call cheap and works for packed files too.
  const auto bytes = os::ReadBytes(file);
  if (!bytes) {
    if (error) *error = std::format("could not read {}", res_path);
    return false;
  }
  ma_decoder decoder;
  if (ma_decoder_init_memory(bytes->data(), bytes->size(), nullptr, &decoder) !=
      MA_SUCCESS) {
    if (error) {
      *error = std::format("could not decode audio {}", res_path);
    }
    return false;
  }
  const ma_uint32 channels = decoder.outputChannels;
  const ma_uint32 sample_rate = decoder.outputSampleRate;
  ma_uint64 frames = 0;
  ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
  ma_decoder_uninit(&decoder);

  nlohmann::json out;
  out["path"] = res_path;
  out["type"] = "AudioClip";
  out["channels"] = channels;
  out["sample_rate"] = sample_rate;
  // WAV and FLAC report their length from the headers; a compressed format
  // may need a scan, and when the decoder cannot say, the field is null
  // rather than guessed.
  if (frames > 0 && sample_rate > 0) {
    out["duration"] = std::round(static_cast<double>(frames) /
                                 static_cast<double>(sample_rate) * 1000.0) /
                      1000.0;
  } else {
    out["duration"] = nullptr;
  }
  if (json_out) *json_out = out.dump(2);
  return true;
}

bool InspectDataAsset(const fs::path& file, const std::string& res_path,
                      std::string* json_out, std::string* error) {
  Asset asset;
  std::string load_error;
  if (!LoadAssetFile(file, asset, &load_error)) {
    if (error) *error = std::format("{}: {}", res_path, load_error);
    return false;
  }
  RegisterBuiltinTypes();
  const TypeRegistry& registry = TypeRegistry::Instance();
  nlohmann::json out;
  out["path"] = res_path;
  out["type"] = asset.type;
  nlohmann::json properties = nlohmann::json::object();
  for (const auto& property : asset.properties) {
    properties[property.key] = VariantToJson(property.value);
  }
  out["properties"] = std::move(properties);
  if (const AssetTypeInfo* info = registry.FindAssetType(asset.type)) {
    nlohmann::json fields = nlohmann::json::array();
    for (const FieldInfo& field : info->fields) {
      fields.push_back({
          {"name", field.name},
          {"type", FieldTypeName(field.type)},
          {"default", VariantToJson(field.default_value)},
          {"tooltip", field.tooltip},
          {"set", asset.properties.Has(field.name)},
      });
    }
    out["fields"] = std::move(fields);
  } else {
    out["registered"] = false;
  }
  if (json_out) *json_out = out.dump(2);
  return true;
}

}  // namespace

bool InspectAsset(const Project& project, std::string_view res_path,
                  std::string* json_out, std::string* error) {
  if (!IsResPath(res_path)) {
    if (error) *error = std::format("not a res:// path: {}", res_path);
    return false;
  }
  const fs::path file = project.Resolve(res_path);
  if (file.empty()) {
    if (error) {
      *error = std::format("{} escapes the project root", res_path);
    }
    return false;
  }
  std::error_code ec;
  if (!fs::exists(file, ec) || ec) {
    if (error) *error = std::format("asset not found: {}", res_path);
    return false;
  }

  if (res_path.ends_with(kAssetExtension)) {
    return InspectDataAsset(file, std::string(res_path), json_out, error);
  }
  const std::string type = AssetDatabase::TypeFromExtension(res_path);
  if (type == "Model") {
    return InspectModel(file, std::string(res_path), json_out, error);
  }
  if (type == "Texture") {
    return InspectTexture(file, std::string(res_path), json_out, error);
  }
  if (type == "AudioClip") {
    return InspectAudioClip(file, std::string(res_path), json_out, error);
  }
  // Every other type (maps, prefabs, fonts, UXML, ...): what it is and how
  // big, nothing more — the loaders that understand those files are the
  // validation side of this module.
  nlohmann::json out;
  out["path"] = res_path;
  out["type"] = type.empty() ? "Unknown" : type;
  if (!os::IsPacked(file)) {
    out["bytes"] = fs::file_size(file, ec);
  }
  if (json_out) *json_out = out.dump(2);
  return true;
}

}  // namespace Mercury::introspection
