#include "project.h"

#include <algorithm>
#include <system_error>

namespace Mercury {

bool IsResPath(std::string_view path) { return path.starts_with(kResPrefix); }

std::shared_ptr<Project> Project::Load(const fs::path& mproj_file,
                                       std::string* error) {
  std::error_code ec;
  fs::path file = fs::weakly_canonical(fs::absolute(mproj_file, ec), ec);
  auto project = std::shared_ptr<Project>(new Project());
  mtext::ParseError parse_error;
  if (!mtext::LoadFile(file, project->document_, &parse_error)) {
    if (error) *error = file.string() + ":" + parse_error.ToString();
    return nullptr;
  }
  if (!project->document_.Find("project")) {
    if (error) *error = file.string() + ": missing [project] section";
    return nullptr;
  }
  project->file_ = file;
  project->root_ = file.parent_path();
  return project;
}

std::shared_ptr<Project> Project::Create(const fs::path& mproj_file,
                                         std::string name) {
  std::error_code ec;
  auto project = std::shared_ptr<Project>(new Project());
  project->file_ = fs::weakly_canonical(fs::absolute(mproj_file, ec), ec);
  project->root_ = project->file_.parent_path();
  auto& section = project->GetOrAddSection("project");
  section.properties.Set("name", name);
  section.properties.Set("main_map", "res://maps/main.mmap");
  section.properties.Set("engine_version", "dev");
  auto& window = project->GetOrAddSection("window");
  window.properties.Set("title", name);
  window.properties.Set("width", 1280);
  window.properties.Set("height", 720);
  return project;
}

bool Project::Save() const { return mtext::SaveFile(file_, document_); }

mtext::Section& Project::GetOrAddSection(std::string_view tag) {
  if (auto section = document_.Find(tag)) return *section;
  return document_.Add(std::string(tag));
}

std::string Project::GetName() const {
  auto section = document_.Find("project");
  return section ? section->properties.GetString("name", "Untitled")
                 : "Untitled";
}

void Project::SetName(std::string name) {
  GetOrAddSection("project").properties.Set("name", std::move(name));
}

std::string Project::GetVersion() const {
  auto section = document_.Find("project");
  return section ? section->properties.GetString("version", "dev") : "dev";
}

std::string Project::GetMainMap() const {
  auto section = document_.Find("project");
  return section ? section->properties.GetString("main_map") : "";
}

void Project::SetMainMap(std::string res_path) {
  GetOrAddSection("project").properties.Set("main_map", std::move(res_path));
}

std::string Project::GetWindowTitle() const {
  auto section = document_.Find("window");
  return section ? section->properties.GetString("title", GetName())
                 : GetName();
}

int Project::GetWindowWidth() const {
  auto section = document_.Find("window");
  return section ? static_cast<int>(section->properties.GetInt("width", 1280))
                 : 1280;
}

int Project::GetWindowHeight() const {
  auto section = document_.Find("window");
  return section ? static_cast<int>(section->properties.GetInt("height", 720))
                 : 720;
}

std::string Project::GetBuildOutput() const {
  auto section = document_.Find("build");
  return section ? section->properties.GetString("output") : "";
}

void Project::SetBuildOutput(std::string path) {
  GetOrAddSection("build").properties.Set("output", std::move(path));
}

glm::vec3 Project::GetGravity() const {
  constexpr glm::vec3 kDefault(0.0f, -9.81f, 0.0f);
  auto section = document_.Find("physics");
  if (!section) return kDefault;
  auto value = section->properties.Get("gravity");
  if (!value) return kDefault;
  auto gravity = value->Get<glm::vec3>();
  return gravity ? *gravity : kDefault;
}

int Project::GetTickRate() const {
  auto section = document_.Find("physics");
  if (!section) return 60;
  const int64_t rate = section->properties.GetInt("tick_rate", 60);
  return rate > 0 && rate <= 1000 ? static_cast<int>(rate) : 60;
}

bool Project::GetQueriesHitTriggers() const {
  auto section = document_.Find("physics");
  return section ? section->properties.GetBool("queries_hit_triggers", true) : true;
}

std::string Project::GetModelConvention() const {
  auto section = document_.Find("import");
  return section ? section->properties.GetString("model_convention", "native") : "native";
}

std::string Project::GetInputActionsPath() const {
  auto section = document_.Find("input");
  return section ? section->properties.GetString("actions") : "";
}

void Project::SetInputActionsPath(std::string res_path) {
  GetOrAddSection("input").properties.Set("actions", std::move(res_path));
}

namespace {
std::vector<std::string> ReadNames(const mtext::Section* section) {
  std::vector<std::string> names;
  if (!section) return names;
  auto value = section->properties.Get("names");
  if (!value) return names;
  if (auto array = value->Get<mtext::Array>()) {
    for (const auto& item : *array) names.push_back(item.ToString().value_or(""));
  }
  return names;
}
}  // namespace

std::vector<std::string> Project::GetLayerNames() const {
  std::vector<std::string> names = ReadNames(document_.Find("layers"));
  if (names.empty()) {
    names = {"Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI"};
  }
  names.resize(32);
  return names;
}

std::string Project::LayerName(int layer) const {
  if (layer < 0 || layer >= 32) return "";
  return GetLayerNames()[layer];
}

int Project::LayerFromName(std::string_view name) const {
  const auto names = GetLayerNames();
  for (size_t i = 0; i < names.size(); i++) {
    if (!names[i].empty() && names[i] == name) return static_cast<int>(i);
  }
  return -1;
}

std::vector<std::string> Project::GetTags() const {
  std::vector<std::string> tags = {"Untagged", "Respawn",    "Finish",
                                   "EditorOnly", "MainCamera", "Player",
                                   "GameController"};
  for (auto& tag : ReadNames(document_.Find("tags"))) {
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
  }
  return tags;
}

fs::path Project::Resolve(std::string_view path) const {
  if (IsResPath(path)) {
    path.remove_prefix(kResPrefix.size());
    return (root_ / fs::path(path)).lexically_normal();
  }
  fs::path p(path);
  if (p.is_absolute()) return p;
  return (root_ / p).lexically_normal();
}

std::string Project::ToResPath(const fs::path& path) const {
  std::error_code ec;
  fs::path absolute = fs::weakly_canonical(fs::absolute(path, ec), ec);
  fs::path relative = absolute.lexically_relative(root_);
  if (relative.empty() || *relative.begin() == "..") return "";
  return std::string(kResPrefix) + relative.generic_string();
}

}  // namespace Mercury
