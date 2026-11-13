#include "editor_scene.h"

#include <algorithm>
#include <format>

#include "debugger/debugger.h"
#include "debugger/profiler.h"
#include "os/process.h"
#include "properties/camera.h"
#include "scripting/type_registry.h"
#include "templates.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {
constexpr size_t kMaxUndoSteps = 200;
}

const EditorScene::PackSizeOption EditorScene::kPackSizes[kNumPackSizes] = {
    {"Single file", 0}, {"100 MB", 100}, {"256 MB", 256},
    {"512 MB", 512},    {"1 GB", 1024},  {"2 GB", 2048},
};

EditorScene::EditorScene(fs::path initial_project, std::string initial_map)
    : MapScene(nullptr, Mode::kEdit),
      initial_project_(std::move(initial_project)),
      initial_map_(std::move(initial_map)),
      play_(console_) {
  settings_.Load();
  PDebug::SetSink([this](PDebug::Level level, const std::string& message) {
    Console::Level console_level = Console::Level::kInfo;
    if (level == PDebug::Level::kWarn) console_level = Console::Level::kWarning;
    if (level == PDebug::Level::kError) console_level = Console::Level::kError;
    console_.Add(console_level, Console::Source::kEditor, message);
  });
  ui_builder_.list_files = [this](const std::vector<std::string>& extensions) {
    return ListProjectFiles(extensions);
  };
}

EditorScene::~EditorScene() { PDebug::SetSink(nullptr); }

void EditorScene::OnStart() {
  MapScene::OnStart();
  if (render_host_) {
    render_host_->EnableSceneTexture(true);
    render_host_->SetShowRenderStats(false);
  }
  std::error_code ec;
  fs::create_directories(EditorSettings::ConfigDir(), ec);

  if (auto camera = Objects().GetProperty<Camera>(GetDefaultCamera())) {
    camera->SetFOV(60.0f);
  }
  UpdateEditorCamera();

  if (!initial_project_.empty() && OpenProjectOrDirectory(initial_project_) &&
      !initial_map_.empty()) {
    OpenMap(initial_map_);
  }
}

void EditorScene::OnDestroy() {
  play_.Stop();
  release_.Cancel();
  MapScene::OnDestroy();
}

void EditorScene::OnTick(double delta_t) {}

bool EditorScene::OnCloseRequested() { return !HasUnsavedChanges(); }

// ------------------------------------------------------------------ Frame ---

void EditorScene::OnFrame(double delta_t) {
  play_.Update();
  release_.Update();
  lore_.Update();
  if (play_.TakeSchemaUpdated()) LoadScriptSchema();

  if (GetProject() && settings_.show_ui_builder && ui_builder_.IsOpen()) {
    const float dt = static_cast<float>(delta_t) / 1000.0f;
    ui_builder_.UpdatePreview(render_host_, dt);
  }

  if (IsModelPreloading() && PollModelPreload() && !pending_import_.empty()) {
    FinishModelImport();
  }
  if (transform_uid_) {
    const MapNode* moved = map_.FindByUid(transform_uid_);
    if (!scene_dirty_ && (!moved || !SyncNodeTransform(*moved))) scene_dirty_ = true;
    transform_uid_ = 0;
  }
  if (scene_dirty_ && GetProject() && !IsModelPreloading()) {
    if (auto jobs = MissingModels(map_); !jobs.empty()) {
      StartModelPreload(std::move(jobs));
    } else {
      SyncFromMap(map_);
      scene_dirty_ = false;
    }
  }
  UpdateEditorCamera();
  Profiler::SetLoading(!GetProject() || scene_dirty_ || IsModelPreloading());
}

std::string EditorScene::WindowTitle() const {
  std::string title = "Mercury Editor";
  if (auto project = GetProject()) {
    title += " - " + project->GetName() + " - " + MapDisplayName();
    if (dirty_) title += " *";
  }
  return title;
}

std::string EditorScene::MapDisplayName() const {
  if (map_path_.empty()) return "(unsaved map)";
  return fs::path(map_path_).filename().string();
}

// ---------------------------------------------------------------- Project ---

bool EditorScene::OpenProject(const fs::path& mproj) {
  std::string error;
  auto project = Project::Load(mproj, &error);
  if (!project) {
    console_.Error("Could not open project: " + error);
    return false;
  }
  play_.Stop();
  release_.Cancel();
  SetProject(project);
  ui_builder_.SetProject(project);
  input_actions_.SetProject(project);
  lore_.SetProject(project);
  settings_.AddRecentProject(project->GetFile());
  settings_.Save();
  game_aspect_ = static_cast<float>(project->GetWindowWidth()) /
                 static_cast<float>(std::max(1, project->GetWindowHeight()));
  LoadScriptSchema();
  console_.Info("Opened project " + project->GetFile().string());

  selected_uid_ = 0;
  selected_asset_.clear();
  ++selection_revision_;
  const std::string main_map = project->GetMainMap();
  if (main_map.empty() || !OpenMap(main_map)) {
    NewMap("");
  }
  ++map_revision_;
  return true;
}

bool EditorScene::OpenProjectOrDirectory(const fs::path& input) {
  fs::path path = input;
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    for (const auto& entry : fs::directory_iterator(path, ec)) {
      if (entry.path().extension() == kProjectExtension) {
        path = entry.path();
        break;
      }
    }
  }
  return OpenProject(path);
}

void EditorScene::CloseProject() {
  play_.Stop();
  release_.Cancel();
  SetProject(nullptr);
  ui_builder_.SetProject(nullptr);
  input_actions_.SetProject(nullptr);
  lore_.SetProject(nullptr);
  map_ = Map();
  map_path_.clear();
  undo_.clear();
  redo_.clear();
  last_committed_ = map_;
  edit_open_ = false;
  coalesced_session_ = false;
  dirty_ = false;
  selected_uid_ = 0;
  selected_asset_.clear();
  TypeRegistry::Instance().ClearSchemaScripts();
  ++schema_revision_;
  ++selection_revision_;
  ++map_revision_;
}

bool EditorScene::HasUnsavedChanges() const {
  return dirty_ || ui_builder_.IsDirty() || input_actions_.IsDirty();
}

void EditorScene::DiscardAllChanges() {
  dirty_ = false;
  ui_builder_.Close();
  input_actions_.Close();
  ++map_revision_;
}

bool EditorScene::SaveAllDirty() {
  const bool map_ok = !dirty_ || SaveMap();
  const bool ui_ok = !ui_builder_.IsDirty() || ui_builder_.Save();
  const bool input_ok = !input_actions_.IsDirty() || input_actions_.Save();
  return map_ok && ui_ok && input_ok;
}

void EditorScene::LoadScriptSchema() {
  auto project = GetProject();
  if (!project) return;
  const fs::path schema = PlaySession::SchemaPath(*project);
  auto& registry = TypeRegistry::Instance();
  if (!fs::exists(schema)) {
    registry.ClearSchemaScripts();
    console_.Info(
        "No script list yet; press Build or Play so the Inspector can show "
        "your scripts.");
    ++schema_revision_;
    return;
  }
  std::string error;
  if (!registry.LoadSchema(schema, &error)) {
    console_.Error("Could not read script list: " + error);
  }
  ++schema_revision_;
}

bool EditorScene::OpenMap(const std::string& res_path) {
  auto project = GetProject();
  if (!project) return false;
  Map loaded;
  std::string error;
  if (!loaded.Load(project->Resolve(res_path), &error)) {
    console_.Error("Could not open map: " + error);
    return false;
  }
  map_ = std::move(loaded);
  map_path_ = res_path;
  last_committed_ = map_;
  undo_.clear();
  redo_.clear();
  edit_open_ = false;
  coalesced_session_ = false;
  dirty_ = false;
  scene_dirty_ = true;
  selected_uid_ = 0;
  selected_asset_.clear();

  view_2d_ = !MapLooks3D();
  FrameMap();
  console_.Info("Opened " + res_path);
  ++selection_revision_;
  ++map_revision_;
  return true;
}

void EditorScene::NewMap(const std::string& res_path) {
  auto project = GetProject();
  Map map;
  std::string error;
  map.LoadFromString(
      Templates::NewMapText(project ? project->GetName() : "Root", !view_2d_),
      &error);
  map_ = std::move(map);
  map_path_ = res_path;
  last_committed_ = map_;
  undo_.clear();
  redo_.clear();
  edit_open_ = false;
  coalesced_session_ = false;
  dirty_ = !res_path.empty();
  scene_dirty_ = true;
  selected_uid_ = 0;
  selected_asset_.clear();
  ++selection_revision_;
  if (!res_path.empty()) SaveMap();
  ++map_revision_;
}

bool EditorScene::SaveMap() {
  CommitEdit();
  auto project = GetProject();
  if (!project) return false;
  if (map_path_.empty()) return false;
  auto type_of = [this](const std::string& path) {
    return Assets().TypeOf(path);
  };
  if (!map_.Save(project->Resolve(map_path_), type_of)) {
    console_.Error("Could not save " + map_path_);
    return false;
  }
  if (project->GetMainMap().empty()) {
    project->SetMainMap(map_path_);
    project->Save();
  }
  dirty_ = false;
  console_.Info("Saved " + map_path_);
  ++map_revision_;
  return true;
}

bool EditorScene::SaveNewMapAs(const std::string& res_path) {
  auto project = GetProject();
  if (!project || res_path.empty()) return false;
  if (fs::exists(project->Resolve(res_path))) {
    console_.Error(res_path + " already exists");
    return false;
  }
  if (map_path_.empty()) {
    map_path_ = res_path;
    return SaveMap();
  }
  NewMap(res_path);
  return true;
}

bool EditorScene::SetMainMap() {
  auto project = GetProject();
  if (!project || map_path_.empty()) return false;
  project->SetMainMap(map_path_);
  if (!project->Save()) return false;
  console_.Info(map_path_ + " is now the map the game starts with");
  ++map_revision_;
  return true;
}

// ---------------------------------------------------------------- Editing ---

void EditorScene::BeginEdit() { coalesced_session_ = true; }

void EditorScene::CommitEdit() {
  if (edit_open_) {
    last_committed_ = map_;
    edit_open_ = false;
  }
  coalesced_session_ = false;
}

void EditorScene::MarkChanged(bool rebuild_scene, bool coalesced) {
  if (!coalesced && !coalesced_session_ && edit_open_) {
    last_committed_ = map_;
    edit_open_ = false;
  }
  if (!edit_open_) {
    undo_.push_back(last_committed_);
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    edit_open_ = true;
  }
  if (!coalesced && !coalesced_session_) {
    last_committed_ = map_;
    edit_open_ = false;
  }
  dirty_ = true;
  if (rebuild_scene) scene_dirty_ = true;
  ++map_revision_;
}

void EditorScene::NotifyTransformEdited(uint64_t uid, bool coalesced) {
  MarkChanged(false, coalesced);
  transform_uid_ = uid;
}

void EditorScene::Undo() {
  CommitEdit();
  if (undo_.empty()) return;
  redo_.push_back(map_);
  map_ = std::move(undo_.back());
  undo_.pop_back();
  last_committed_ = map_;
  dirty_ = true;
  scene_dirty_ = true;
  ++selection_revision_;
  ++map_revision_;
}

void EditorScene::Redo() {
  CommitEdit();
  if (redo_.empty()) return;
  undo_.push_back(map_);
  map_ = std::move(redo_.back());
  redo_.pop_back();
  last_committed_ = map_;
  dirty_ = true;
  scene_dirty_ = true;
  ++selection_revision_;
  ++map_revision_;
}

MapNode* EditorScene::GetSelectedNode() {
  return selected_uid_ ? map_.FindByUid(selected_uid_) : nullptr;
}

const MapNode* EditorScene::GetSelectedNode() const {
  return const_cast<EditorScene*>(this)->GetSelectedNode();
}

void EditorScene::Select(const MapNode* node) {
  const uint64_t next = node ? node->uid : 0;
  if (next == selected_uid_ && selected_asset_.empty()) return;
  selected_uid_ = next;
  if (node) selected_asset_.clear();
  ++selection_revision_;
}

void EditorScene::SelectAsset(const std::string& res_path) {
  if (selected_asset_ == res_path && selected_uid_ == 0) return;
  selected_asset_ = res_path;
  selected_uid_ = 0;
  ++selection_revision_;
}

uint64_t EditorScene::ConsumeRenameRequest() {
  const uint64_t uid = rename_uid_;
  rename_uid_ = 0;
  return uid;
}

MapNode* EditorScene::CreateNode(MapNode* parent, const std::string& name) {
  MapNode* node = map_.AddNode(parent ? parent : map_.GetRoot(), name);
  node->components.push_back({"Transform", {}});
  Select(node);
  MarkChanged();
  return node;
}

bool EditorScene::RenameNode(MapNode* node, const std::string& name) {
  if (!node || name.empty() || name == node->name) return false;
  map_.RenameNode(node, name);
  MarkChanged();
  return true;
}

bool EditorScene::ReparentNode(MapNode* node, MapNode* new_parent, int index) {
  if (!node) return false;
  if (!new_parent) new_parent = map_.GetRoot();
  if (!map_.ReparentNode(node, new_parent, index)) return false;
  MarkChanged();
  return true;
}

bool EditorScene::AddComponent(MapNode* node, const std::string& type) {
  if (!node || node->FindComponent(type)) return false;
  node->components.push_back({type, {}});
  MarkChanged();
  return true;
}

bool EditorScene::RemoveComponent(MapNode* node, size_t index) {
  if (!node || index >= node->components.size()) return false;
  node->components.erase(node->components.begin() + static_cast<ptrdiff_t>(index));
  MarkChanged();
  return true;
}

bool EditorScene::MoveComponent(MapNode* node, size_t from, size_t to) {
  if (!node || from >= node->components.size() || to >= node->components.size() ||
      from == to) {
    return false;
  }
  std::swap(node->components[from], node->components[to]);
  MarkChanged();
  return true;
}

bool EditorScene::ResetComponentToDefaults(MapNode* node, size_t index) {
  if (!node || index >= node->components.size()) return false;
  node->components[index].properties.clear();
  MarkChanged();
  return true;
}

bool EditorScene::SaveAsset(Asset& asset) {
  if (!Assets().Save(asset)) return false;
  scene_dirty_ = true;
  ++map_revision_;
  return true;
}

void EditorScene::ImportModel(const std::string& res_path) {
  if (!GetProject() || IsModelPreloading()) return;
  pending_import_ = res_path;
  if (HasModelAsset(res_path)) {
    FinishModelImport();
  } else {
    StartModelPreload({MakeModelJob(res_path)});
  }
}

void EditorScene::FinishModelImport() {
  const std::string res_path = std::move(pending_import_);
  pending_import_.clear();
  auto model = GetModelAsset(res_path);
  if (!model) {
    console_.Error("Could not import " + res_path);
    return;
  }

  const auto& nodes = model->Nodes();
  std::map<std::string, int> seen;
  std::vector<std::pair<std::string, std::string>> parts;
  for (size_t i = 1; i < nodes.size(); i++) {
    const int count = ++seen[nodes[i].name];
    if (nodes[i].meshes.empty()) continue;
    parts.emplace_back(
        nodes[i].name,
        count == 1 ? nodes[i].name : nodes[i].name + "#" + std::to_string(count));
  }
  if (parts.empty()) {
    console_.Warn(res_path + " has nothing to import");
    return;
  }

  const std::string stem = fs::path(res_path).stem().string();
  MapNode* parent = CreateNode(nullptr, stem.empty() ? "Model" : stem);
  for (const auto& [name, model_node] : parts) {
    MapNode* node = map_.AddNode(parent, name);
    node->components.push_back({"Transform", {}});
    ComponentData renderer{"MeshRenderer", {}};
    renderer.properties.Set("mesh", std::string("model"));
    renderer.properties.Set("model", mtext::Resource{res_path});
    renderer.properties.Set("node", model_node);
    node->components.push_back(std::move(renderer));
  }
  Select(parent);
  MarkChanged();
  console_.Info("Imported " + std::to_string(parts.size()) + " objects from " +
                res_path);
}

MapNode* EditorScene::CreatePreset(MapNode* parent, Preset preset) {
  static constexpr const char* kNames[] = {
      "Node",      "Cube",      "Sphere",    "Capsule",
      "Cylinder",  "Plane",     "Quad",      "Sprite",
      "DirectionalLight", "Camera", "PointLight", "AreaLight",
      "LightProbeVolume",
  };
  if (!parent) parent = map_.GetRoot();
  MapNode* node = map_.AddNode(parent, kNames[static_cast<int>(preset)]);

  glm::vec3 world = camera_target_;
  if (view_2d_) world.z = 0.0f;
  glm::vec3 parent_world(0.0f);
  if (auto transform =
          Objects().GetProperty<Transform>(GetEntityForNode(parent->uid))) {
    parent_world = transform->GetPosition();
  }
  glm::vec3 position = glm::round((world - parent_world) * 100.0f) / 100.0f;
  glm::vec3 rotation(0.0f);
  float size = 1.0f;
  if (view_2d_) {
    size = std::pow(10.0f, std::round(std::log10(WorldPerPixel(world) * 80.0f)));
  }
  glm::vec3 scale(size);

  auto mesh = [&](const char* shape) {
    ComponentData renderer{"MeshRenderer", {}};
    if (shape) renderer.properties.Set("mesh", std::string(shape));
    node->components.push_back(std::move(renderer));
  };
  switch (preset) {
    case Preset::kEmpty: break;
    case Preset::kCube:
      mesh(nullptr);
      node->components.push_back({"BoxCollider", {}});
      break;
    case Preset::kSphere:
      mesh("sphere");
      node->components.push_back({"SphereCollider", {}});
      break;
    case Preset::kCapsule:
      mesh("capsule");
      node->components.push_back({"CapsuleCollider", {}});
      break;
    case Preset::kCylinder:
      mesh("cylinder");
      node->components.push_back({"CylinderCollider", {}});
      break;
    case Preset::kPlane: {
      mesh("plane");
      ComponentData collider{"BoxCollider", {}};
      collider.properties.Set("size", glm::vec3(1.0f, 0.01f, 1.0f));
      node->components.push_back(std::move(collider));
      scale = glm::vec3(10.0f * size, 1.0f, 10.0f * size);
      break;
    }
    case Preset::kQuad: mesh("quad"); break;
    case Preset::kSprite: node->components.push_back({"SpriteRenderer", {}}); break;
    case Preset::kDirectionalLight:
      rotation = glm::vec3(-50.0f, -30.0f, 0.0f);
      scale = glm::vec3(1.0f);
      node->components.push_back({"DirectionalLight", {}});
      break;
    case Preset::kPointLight:
      scale = glm::vec3(1.0f);
      node->components.push_back({"PointLight", {}});
      break;
    case Preset::kAreaLight:
      rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
      scale = glm::vec3(1.0f);
      node->components.push_back({"AreaLight", {}});
      break;
    case Preset::kLightProbeVolume:
      scale = glm::vec3(1.0f);
      node->components.push_back({"LightProbeVolume", {}});
      break;
    case Preset::kCamera: {
      ComponentData camera{"Camera", {}};
      scale = glm::vec3(1.0f);
      if (view_2d_) {
        const float fit = WorldPerPixel(world) * scene_size_.y;
        camera.properties.Set("fit_height", static_cast<double>(std::round(fit)));
      } else {
        position = glm::round((CameraPosition() - parent_world) * 100.0f) / 100.0f;
        rotation = glm::round(glm::vec3(camera_pitch_, camera_yaw_, 0.0f));
      }
      bool has_camera = false;
      map_.Visit([&](const MapNode& other) {
        if (&other != node && other.FindComponent("Camera")) has_camera = true;
      });
      if (has_camera) camera.properties.Set("current", false);
      node->components.push_back(std::move(camera));
      break;
    }
  }

  ComponentData transform{"Transform", {}};
  if (position != glm::vec3(0.0f)) transform.properties.Set("position", position);
  if (rotation != glm::vec3(0.0f)) transform.properties.Set("rotation", rotation);
  if (scale != glm::vec3(1.0f)) transform.properties.Set("scale", scale);
  node->components.insert(node->components.begin(), std::move(transform));

  Select(node);
  MarkChanged();
  return node;
}

void EditorScene::DeleteSelected() {
  MapNode* node = GetSelectedNode();
  if (!node || node->IsRoot()) return;
  MapNode* parent = node->parent;
  map_.RemoveNode(node);
  Select(parent);
  MarkChanged();
}

void EditorScene::DuplicateSelected() {
  MapNode* node = GetSelectedNode();
  if (!node || node->IsRoot()) return;
  if (MapNode* copy = map_.DuplicateNode(node)) {
    Select(copy);
    MarkChanged();
  }
}

// ----------------------------------------------------------------- Assets ---

std::vector<std::string> EditorScene::ListProjectFiles(
    const std::vector<std::string>& extensions) const {
  std::vector<std::string> files;
  auto project = GetProject();
  if (!project) return files;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(project->GetRoot(), ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const auto name = it->path().filename().string();
    if (it->is_directory() && (name.starts_with(".") || name == "build")) {
      it.disable_recursion_pending();
      continue;
    }
    std::string extension = it->path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (std::find(extensions.begin(), extensions.end(), extension) !=
        extensions.end()) {
      files.push_back(project->ToResPath(it->path()));
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

void EditorScene::CreateAsset(const fs::path& dir, const std::string& type) {
  auto project = GetProject();
  if (!project) return;
  std::string base = type == "PhysicsMaterial" ? "physics_material"
                     : type == "PanelSettings" ? "panel_settings"
                                               : "material";
  fs::path file = dir / (base + std::string(kAssetExtension));
  for (int i = 2; fs::exists(file); i++) {
    file = dir / (base + std::to_string(i) + std::string(kAssetExtension));
  }
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (Templates::CreateAsset(file, type)) {
    SelectAsset(project->ToResPath(file));
  } else {
    console_.Error("Could not create " + file.string());
  }
}

bool EditorScene::CreateScript(const std::string& class_name) {
  auto project = GetProject();
  if (!project) return false;
  std::string error;
  auto file = Templates::CreateScript(project->GetRoot(), class_name, &error);
  if (file.empty()) {
    console_.Error("Could not create script: " + error);
    return false;
  }
  console_.Info("Created " + file.string() +
                ". Build to add it to the Add Component menu.");
  OpenInExternalEditor(file);
  return true;
}

bool EditorScene::CreateFolder(const fs::path& dir, const std::string& name) {
  std::error_code ec;
  const bool ok = fs::create_directories(dir / name, ec);
  if (ec) {
    console_.Error("Could not create folder: " + ec.message());
    return false;
  }
  return ok || fs::is_directory(dir / name);
}

bool EditorScene::DeleteProjectPath(const fs::path& target) {
  std::error_code ec;
  fs::remove_all(target, ec);
  if (ec) {
    console_.Error("Could not delete: " + ec.message());
    return false;
  }
  selected_asset_.clear();
  ++selection_revision_;
  return true;
}

bool EditorScene::BuildGame(const std::string& output_str) {
  auto project = GetProject();
  if (!project) return false;
  const fs::path output = project->Resolve(output_str);
  std::error_code ec;
  if (output_str.empty() || fs::equivalent(output, project->GetRoot(), ec)) {
    console_.Error("Invalid build output folder");
    return false;
  }
  if (output_str != project->GetBuildOutput()) {
    project->SetBuildOutput(output_str);
    if (!project->Save()) console_.Error("Could not save the project file");
  }
  if (!SaveMap()) return false;
  release_.Start(*project, output,
                 static_cast<uint64_t>(settings_.pack_size_mb) << 20);
  return true;
}

void EditorScene::OpenInUIBuilder(const std::string& res_path) {
  if (ui_builder_.IsOpen() && ui_builder_.path() == res_path) {
    if (!settings_.show_ui_builder) {
      settings_.show_ui_builder = true;
      settings_.Save();
    }
    focus_ui_builder_ = true;
    return;
  }
  if (ui_builder_.IsDirty()) ui_builder_.Save();
  if (ui_builder_.Open(res_path)) {
    if (!settings_.show_ui_builder) {
      settings_.show_ui_builder = true;
      settings_.Save();
    }
    focus_ui_builder_ = true;
  }
}

void EditorScene::OpenInInputActions(const std::string& res_path) {
  if (input_actions_.path() != res_path) {
    if (input_actions_.IsDirty()) input_actions_.Save();
    if (!input_actions_.Open(res_path)) return;
  }
  if (!settings_.show_input_actions) {
    settings_.show_input_actions = true;
    settings_.Save();
  }
  focus_input_actions_ = true;
}

bool EditorScene::ConsumeUIBuilderFocusRequest() {
  const bool v = focus_ui_builder_;
  focus_ui_builder_ = false;
  return v;
}

bool EditorScene::ConsumeInputActionsFocusRequest() {
  const bool v = focus_input_actions_;
  focus_input_actions_ = false;
  return v;
}

void EditorScene::CreateInputActionsFile(const fs::path& dir) {
  auto project = GetProject();
  if (!project) return;
  std::error_code ec;
  fs::create_directories(dir, ec);
  fs::path file = dir / ("actions" + std::string(kInputActionsExtension));
  for (int i = 2; fs::exists(file); i++) {
    file = dir / ("actions" + std::to_string(i) +
                  std::string(kInputActionsExtension));
  }
  if (!Templates::CreateInputActions(file)) {
    console_.Error("Could not create " + file.string());
    return;
  }
  const std::string res_path = project->ToResPath(file);
  console_.Info("Created " + res_path);
  if (project->GetInputActionsPath().empty()) {
    project->SetInputActionsPath(res_path);
    project->Save();
  }
  OpenInInputActions(res_path);
}

void EditorScene::CreateUIFile(const fs::path& dir, const std::string& kind) {
  auto project = GetProject();
  if (!project) return;
  std::error_code ec;
  fs::create_directories(dir, ec);
  const bool uxml = kind == "uxml";
  const std::string base = uxml ? "new_document" : "new_style";
  const std::string extension =
      uxml ? std::string(kUxmlExtension) : std::string(kUssExtension);
  fs::path file = dir / (base + extension);
  for (int i = 2; fs::exists(file); i++) {
    file = dir / (base + std::to_string(i) + extension);
  }
  const bool ok = uxml ? Templates::CreateUxml(file) : Templates::CreateUss(file);
  if (!ok) {
    console_.Error("Could not create " + file.string());
    return;
  }
  const std::string res_path = project->ToResPath(file);
  console_.Info("Created " + res_path);
  if (uxml) OpenInUIBuilder(res_path);
}

void EditorScene::OpenInExternalEditor(const fs::path& file) {
  auto project = GetProject();
  if (settings_.external_editor.empty()) {
    if (!os::OpenWithDefaultApp(file)) {
      console_.Error("Could not open " + file.string());
    }
    return;
  }
  std::vector<std::string> args;
  std::string current;
  bool quoted = false;
  for (char c : settings_.external_editor) {
    if (c == '"') {
      quoted = !quoted;
    } else if (c == ' ' && !quoted) {
      if (!current.empty()) args.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) args.push_back(std::move(current));
  for (auto& arg : args) {
    auto replace = [&](const std::string& key, const std::string& value) {
      for (size_t pos; (pos = arg.find(key)) != std::string::npos;) {
        arg.replace(pos, key.size(), value);
      }
    };
    replace("{file}", file.string());
    replace("{project}", project ? project->GetRoot().string() : "");
  }
  std::string error;
  if (!os::LaunchDetached(args, &error)) {
    console_.Error("External editor: " + error);
  }
}

}  // namespace Mercury
