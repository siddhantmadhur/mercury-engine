#include "editor_scene.h"

#include <algorithm>
#include <format>

#include "debugger/debugger.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "os/process.h"
#include "properties/camera.h"
#include "scripting/type_registry.h"
#include "templates.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

constexpr const char* kHierarchyWindow = "Hierarchy";
constexpr const char* kInspectorWindow = "Inspector";
constexpr const char* kSceneWindow = "Scene";
constexpr const char* kProjectWindow = "Project";
constexpr const char* kConsoleWindow = "Console";
constexpr const char* kUIBuilderWindow = UIBuilder::kWindowName;
constexpr const char* kLoreWindow = LoreVcs::kWindowName;
constexpr size_t kMaxUndoSteps = 200;

void ApplyEditorStyle() {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 3.0f;
  style.TabRounding = 3.0f;
  style.GrabRounding = 3.0f;
  style.WindowPadding = ImVec2(8, 8);
  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 5);
  ImVec4* colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.0f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
  colors[ImGuiCol_Header] = ImVec4(0.22f, 0.32f, 0.48f, 0.60f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.39f, 0.58f, 0.80f);
  colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.32f, 0.48f, 1.0f);
}

}  // namespace

EditorScene::EditorScene(fs::path initial_project)
    : MapScene(nullptr, Mode::kEdit),
      initial_project_(std::move(initial_project)),
      play_(console_) {
  settings_.Load();
  // Mirror engine logs into the Console panel.
  PDebug::SetSink([this](PDebug::Level level, const std::string& message) {
    Console::Level console_level = Console::Level::kInfo;
    if (level == PDebug::Level::kWarn) console_level = Console::Level::kWarning;
    if (level == PDebug::Level::kError) console_level = Console::Level::kError;
    console_.Add(console_level, Console::Source::kEditor, message);
  });
  std::snprintf(new_project_dir_, sizeof(new_project_dir_), "%s",
                EditorSettings::HomeDir().string().c_str());
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

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // Keep the panel layout with the user's settings, not in the working dir.
  static std::string ini_path;
  std::error_code ec;
  fs::create_directories(EditorSettings::ConfigDir(), ec);
  ini_path = (EditorSettings::ConfigDir() / "editor_layout.ini").string();
  io.IniFilename = ini_path.c_str();
  ApplyEditorStyle();

  if (auto camera = Objects().GetProperty<Camera>(GetDefaultCamera())) {
    camera->SetFOV(60.0f);
  }
  UpdateEditorCamera();

  if (!initial_project_.empty()) OpenProject(initial_project_);
}

void EditorScene::OnDestroy() {
  play_.Stop();
  release_.Cancel();
  MapScene::OnDestroy();
}

void EditorScene::OnTick(double delta_t) {}

bool EditorScene::OnCloseRequested() {
  if (!dirty_ && !ui_builder_.IsDirty()) return true;
  modal_ = Modal::kUnsaved;
  open_modal_ = true;
  after_unsaved_ = AfterSave::kQuit;
  return false;
}

// ------------------------------------------------------------------ Frame ---

void EditorScene::OnFrame(double delta_t) {
  play_.Update();
  release_.Update();
  lore_.Update();
  if (play_.TakeSchemaUpdated()) LoadScriptSchema();

  project_scan_timer_ += delta_t / 1000.0;
  HandleShortcuts();

  if (!GetProject()) {
    // Keep the docked layout alive behind the launcher; ImGui would
    // otherwise undock every panel while the dockspace is not drawn.
    ImGui::DockSpaceOverViewport(ImGui::GetID("EditorDockspace"),
                                 ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_KeepAliveOnly);
    DrawLauncher();
  } else {
    DrawDockspace();
    DrawHierarchy();
    DrawInspector();
    DrawScene();
    DrawProjectBrowser();
    DrawConsole();
    if (settings_.show_lore) {
      // Layouts saved before the Lore panel existed: put it beside the
      // Console the first time it shows.
      if (ImGuiWindow* console = ImGui::FindWindowByName(kConsoleWindow);
          console && console->DockId) {
        ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
      }
      lore_.Draw(&settings_.show_lore);
      if (!settings_.show_lore) settings_.Save();
    }
    if (settings_.show_ui_builder) {
      ui_builder_.Draw(render_host_, &settings_.show_ui_builder);
      if (!settings_.show_ui_builder) settings_.Save();
    }
  }
  DrawModals();
  DrawModelLoadingPopup();

  EndFrameEdits();
  // Models the map needs are loaded on background threads first, so the
  // rebuild below finds them cached instead of freezing the editor.
  if (IsModelPreloading() && PollModelPreload() && !pending_import_.empty()) {
    FinishModelImport();
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
  UpdateWindowTitle();
}

void EditorScene::UpdateWindowTitle() {
  std::string title = "Mercury Editor";
  if (auto project = GetProject()) {
    title += " - " + project->GetName() + " - " + MapDisplayName();
    if (dirty_) title += " *";
  }
  static std::string last_title;
  if (title != last_title && render_host_) {
    render_host_->SetWindowTitle(title);
    last_title = title;
  }
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
  lore_.SetProject(project);
  settings_.AddRecentProject(project->GetFile());
  settings_.Save();
  game_aspect_ = static_cast<float>(project->GetWindowWidth()) /
                 static_cast<float>(std::max(1, project->GetWindowHeight()));
  LoadScriptSchema();
  console_.Info("Opened project " + project->GetFile().string());

  selected_uid_ = 0;
  selected_asset_.clear();
  const std::string main_map = project->GetMainMap();
  if (main_map.empty() || !OpenMap(main_map)) {
    NewMap("");
  }
  if (!fs::exists(project->GetCacheDir() / "player.mcfg") &&
      !fs::exists(project->GetRoot() / "CMakeLists.txt")) {
    console_.Warn(
        "This project has no CMakeLists.txt, so Play cannot build it yet.");
  }
  return true;
}

void EditorScene::CloseProject() {
  play_.Stop();
  release_.Cancel();
  SetProject(nullptr);
  ui_builder_.SetProject(nullptr);
  lore_.SetProject(nullptr);
  map_ = Map();
  map_path_.clear();
  undo_.clear();
  redo_.clear();
  dirty_ = false;
  selected_uid_ = 0;
  selected_asset_.clear();
  TypeRegistry::Instance().ClearSchemaScripts();
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
    return;
  }
  std::string error;
  if (!registry.LoadSchema(schema, &error)) {
    console_.Error("Could not read script list: " + error);
  }
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
  dirty_ = false;
  scene_dirty_ = true;
  selected_uid_ = 0;

  // Start in the view that suits the map, framed around its contents.
  view_2d_ = !MapLooks3D();
  FrameMap();
  console_.Info("Opened " + res_path);
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
  dirty_ = !res_path.empty();
  scene_dirty_ = true;
  selected_uid_ = 0;
  if (!res_path.empty()) SaveMap();
}

bool EditorScene::SaveMap() {
  auto project = GetProject();
  if (!project) return false;
  if (map_path_.empty()) {
    modal_ = Modal::kNewMap;
    open_modal_ = true;
    new_item_dir_ = project->GetRoot() / "maps";
    return false;
  }
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
  return true;
}

// ---------------------------------------------------------------- Editing ---

void EditorScene::MarkChanged() {
  if (!edit_open_) {
    undo_.push_back(last_committed_);
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    edit_open_ = true;
  }
  dirty_ = true;
  scene_dirty_ = true;
}

void EditorScene::EndFrameEdits() {
  // A drag or a text field keeps one undo step open until it is released.
  if (edit_open_ && !ImGui::IsAnyItemActive()) {
    last_committed_ = map_;
    edit_open_ = false;
  }
}

void EditorScene::Undo() {
  EndFrameEdits();
  if (edit_open_) {
    last_committed_ = map_;
    edit_open_ = false;
  }
  if (undo_.empty()) return;
  redo_.push_back(map_);
  map_ = std::move(undo_.back());
  undo_.pop_back();
  last_committed_ = map_;
  dirty_ = true;
  scene_dirty_ = true;
}

void EditorScene::Redo() {
  if (redo_.empty()) return;
  undo_.push_back(map_);
  map_ = std::move(redo_.back());
  redo_.pop_back();
  last_committed_ = map_;
  dirty_ = true;
  scene_dirty_ = true;
}

MapNode* EditorScene::GetSelectedNode() {
  return selected_uid_ ? map_.FindByUid(selected_uid_) : nullptr;
}

void EditorScene::Select(const MapNode* node) {
  selected_uid_ = node ? node->uid : 0;
  if (node) selected_asset_.clear();
}

MapNode* EditorScene::CreateNode(MapNode* parent, const std::string& name) {
  MapNode* node = map_.AddNode(parent ? parent : map_.GetRoot(), name);
  node->components.push_back({"Transform", {}});
  Select(node);
  MarkChanged();
  return node;
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

  // The objects of an .obj are the root's children; others keep their
  // nesting out of this, so only nodes that draw something are added.
  const auto& nodes = model->Nodes();
  std::map<std::string, int> seen;  // how many nodes of each name so far
  std::vector<std::pair<std::string, std::string>> parts;  // node name, model node
  for (size_t i = 1; i < nodes.size(); i++) {
    const int count = ++seen[nodes[i].name];
    if (nodes[i].meshes.empty()) continue;
    // Several nodes may share a name; "Name#2" picks the second.
    parts.emplace_back(nodes[i].name,
                       count == 1 ? nodes[i].name : nodes[i].name + "#" + std::to_string(count));
  }
  if (parts.empty()) {
    console_.Warn(res_path + " has nothing to import");
    return;
  }

  // Objects keep the positions the file gives them, so the parent sits at the
  // origin; move it to move everything.
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
  console_.Info("Imported " + std::to_string(parts.size()) + " objects from " + res_path);
}

void EditorScene::DrawCreateMenuItems(MapNode* parent) {
  if (ImGui::MenuItem("Empty")) CreatePreset(parent, Preset::kEmpty);
  if (ImGui::BeginMenu("3D Object")) {
    if (ImGui::MenuItem("Cube")) CreatePreset(parent, Preset::kCube);
    if (ImGui::MenuItem("Sphere")) CreatePreset(parent, Preset::kSphere);
    if (ImGui::MenuItem("Capsule")) CreatePreset(parent, Preset::kCapsule);
    if (ImGui::MenuItem("Cylinder")) CreatePreset(parent, Preset::kCylinder);
    if (ImGui::MenuItem("Plane")) CreatePreset(parent, Preset::kPlane);
    if (ImGui::MenuItem("Quad")) CreatePreset(parent, Preset::kQuad);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("2D Object")) {
    if (ImGui::MenuItem("Sprite")) CreatePreset(parent, Preset::kSprite);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Light")) {
    if (ImGui::MenuItem("Directional Light")) {
      CreatePreset(parent, Preset::kDirectionalLight);
    }
    if (ImGui::MenuItem("Point Light")) CreatePreset(parent, Preset::kPointLight);
    if (ImGui::MenuItem("Area Light")) CreatePreset(parent, Preset::kAreaLight);
    if (ImGui::MenuItem("Light Probe Volume")) {
      CreatePreset(parent, Preset::kLightProbeVolume);
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Camera")) CreatePreset(parent, Preset::kCamera);
}

MapNode* EditorScene::CreatePreset(MapNode* parent, Preset preset) {
  static constexpr const char* kNames[] = {
      "Node", "Cube",   "Sphere", "Capsule",          "Cylinder",
      "Plane", "Quad", "Sprite", "DirectionalLight", "Camera", "PointLight",
      "AreaLight", "LightProbeVolume",
  };
  if (!parent) parent = map_.GetRoot();
  MapNode* node = map_.AddNode(parent, kNames[static_cast<int>(preset)]);

  // New things appear where the Scene view is looking. Positions in the map
  // are relative to the parent.
  glm::vec3 world = camera_target_;
  if (view_2d_) world.z = 0.0f;
  glm::vec3 parent_world(0.0f);
  if (auto transform =
          Objects().GetProperty<Transform>(GetEntityForNode(parent->uid))) {
    parent_world = transform->GetPosition();
  }
  glm::vec3 position = glm::round((world - parent_world) * 100.0f) / 100.0f;
  glm::vec3 rotation(0.0f);
  // In the 2D view one unit can be a few pixels, so size new things to the
  // view (a power of ten about 80 pixels across).
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
      rotation = glm::vec3(-90.0f, 0.0f, 0.0f);  // shining down
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
        // Frame what the Scene view shows now.
        const float fit = WorldPerPixel(world) * scene_size_.y;
        camera.properties.Set("fit_height", static_cast<double>(std::round(fit)));
      } else {
        // Like Unity's "Align With View".
        position = glm::round((CameraPosition() - parent_world) * 100.0f) / 100.0f;
        rotation = glm::round(glm::vec3(camera_pitch_, camera_yaw_, 0.0f));
      }
      // A second camera should not take over from the existing one.
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

void EditorScene::HandleShortcuts() {
  if (!GetProject()) return;
  ImGuiIO& io = ImGui::GetIO();
  const bool typing = io.WantTextInput;
  auto chord = [](ImGuiKeyChord c) {
    return ImGui::IsKeyChordPressed(c);
  };
  // The UI Builder has its own document and undo history.
  if (settings_.show_ui_builder && ui_builder_.HasFocus()) {
    if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) ui_builder_.Save();
    if (typing) return;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) ui_builder_.Undo();
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Y) ||
        chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
      ui_builder_.Redo();
    }
    return;
  }
  if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) SaveMap();
  if (chord(ImGuiMod_Ctrl | ImGuiKey_P)) {
    if (play_.IsBusy()) {
      play_.Stop();
    } else if (!release_.IsBusy() && SaveMap()) {
      play_.Play(*GetProject(), map_path_);
    }
  }
  if (typing) return;
  if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) Undo();
  if (chord(ImGuiMod_Ctrl | ImGuiKey_Y) ||
      chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
    Redo();
  }
  if (chord(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelected();
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected_asset_.empty()) {
    DeleteSelected();
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && GetSelectedNode()) {
    rename_uid_ = selected_uid_;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl &&
      !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    FrameSelected();
  }
}

// ----------------------------------------------------------------- Layout ---

void EditorScene::DrawDockspace() {
  DrawMenuBar();
  ImGuiID dockspace = ImGui::DockSpaceOverViewport(
      ImGui::GetID("EditorDockspace"), ImGui::GetMainViewport());
  // First run (no saved layout), a saved layout that lost its splits, or
  // Window > Reset Layout.
  ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
  if (reset_layout_ || !node || !node->IsSplitNode()) {
    BuildDefaultLayout(dockspace);
    reset_layout_ = false;
  }
}

void EditorScene::BuildDefaultLayout(ImGuiID dockspace) {
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
  ImGuiID center = dockspace;
  ImGuiID left = 0, right = 0, bottom = 0;
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, &right, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);
  ImGui::DockBuilderDockWindow(kHierarchyWindow, left);
  ImGui::DockBuilderDockWindow(kInspectorWindow, right);
  ImGui::DockBuilderDockWindow(kProjectWindow, bottom);
  ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
  ImGui::DockBuilderDockWindow(kLoreWindow, bottom);
  ImGui::DockBuilderDockWindow(kSceneWindow, center);
  ImGui::DockBuilderDockWindow(kUIBuilderWindow, center);
  ImGui::DockBuilderFinish(dockspace);
}

void EditorScene::DrawMenuBar() {
  if (!ImGui::BeginMainMenuBar()) return;
  auto project = GetProject();

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Map...")) {
      modal_ = Modal::kNewMap;
      open_modal_ = true;
      new_item_dir_ = project->GetRoot() / "maps";
    }
    if (ImGui::MenuItem("Save Map", "Ctrl+S")) SaveMap();
    if (ImGui::MenuItem("Set As Main Map", nullptr, false,
                        !map_path_.empty() &&
                            project->GetMainMap() != map_path_)) {
      project->SetMainMap(map_path_);
      project->Save();
      console_.Info(map_path_ + " is now the map the game starts with");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Close Project")) {
      if (dirty_ || ui_builder_.IsDirty()) {
        modal_ = Modal::kUnsaved;
        open_modal_ = true;
        after_unsaved_ = AfterSave::kCloseProject;
      } else {
        CloseProject();
      }
    }
    if (ImGui::MenuItem("Quit")) {
      if (OnCloseRequested()) QuitScene();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) Redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
                        GetSelectedNode() != nullptr)) {
      DuplicateSelected();
    }
    if (ImGui::MenuItem("Delete", "Del", false,
                        GetSelectedNode() && !GetSelectedNode()->IsRoot())) {
      DeleteSelected();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("GameObject")) {
    DrawCreateMenuItems(nullptr);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Project")) {
    const bool busy = play_.IsBusy() || release_.IsBusy();
    if (ImGui::MenuItem("Build", nullptr, false, !busy)) {
      play_.Build(*project);
    }
    if (ImGui::MenuItem("Build Game...", nullptr, false, !busy)) {
      build_output_ = project->GetBuildOutput();
      if (build_output_.empty()) build_output_ = ReleaseBuild::DefaultOutput(*project);
      modal_ = Modal::kBuildGame;
      open_modal_ = true;
    }
    ImGui::SetItemTooltip(
        "Build a Release executable with its assets packed into .mpack files");
    if (ImGui::MenuItem("Reload Script List")) LoadScriptSchema();
    if (ImGui::MenuItem("Open Folder")) os::OpenWithDefaultApp(project->GetRoot());
    if (ImGui::MenuItem("Open in External Editor")) {
      OpenInExternalEditor(project->GetRoot());
    }
    ImGui::Separator();
    ImGui::TextDisabled("External editor command");
    ImGui::SetNextItemWidth(320);
    if (ImGui::InputTextWithHint("##external", "e.g. code {project} {file}",
                                 &settings_.external_editor)) {
      settings_.Save();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    if (ImGui::MenuItem("UI Builder", nullptr, &settings_.show_ui_builder)) {
      settings_.Save();
    }
    if (ImGui::MenuItem("Lore", nullptr, &settings_.show_lore)) settings_.Save();
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Layout")) reset_layout_ = true;
    ImGui::EndMenu();
  }

  DrawToolbar();
  ImGui::EndMainMenuBar();
}

void EditorScene::DrawToolbar() {
  auto project = GetProject();
  const float width = ImGui::GetWindowWidth();
  ImGui::SetCursorPosX(width * 0.5f - 90.0f);
  const auto state = play_.GetState();
  if (release_.IsBusy()) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("  Stop  ")) release_.Cancel();
    ImGui::PopStyleColor();
    const auto release_state = release_.GetState();
    const char* status =
        release_state == ReleaseBuild::State::kConfiguring ? "Configuring Release..."
        : release_state == ReleaseBuild::State::kBuilding  ? "Building game..."
                                                           : "Packing...";
    ImGui::TextDisabled("%s", status);
  } else if (state == PlaySession::State::kIdle) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.22f, 1.0f));
    if (ImGui::Button("  Play  ")) {
      if (SaveMap()) play_.Play(*project, map_path_);
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Save, build and run the game (Ctrl+P)");
    if (ImGui::Button("Build")) play_.Build(*project);
    ImGui::SetItemTooltip("Compile the scripts and refresh the Inspector");
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("  Stop  ")) play_.Stop();
    ImGui::PopStyleColor();
    const char* status = state == PlaySession::State::kConfiguring ? "Configuring..."
                         : state == PlaySession::State::kBuilding  ? "Building..."
                                                                   : "Game running";
    ImGui::TextDisabled("%s", status);
  }

  const std::string fps = std::format("{} FPS", render_host_ ? render_host_->GetAverageFps() : 0);
  ImGui::SetCursorPosX(width - ImGui::CalcTextSize(fps.c_str()).x - 12.0f);
  ImGui::TextDisabled("%s", fps.c_str());
}

// ----------------------------------------------------------------- Modals ---

void EditorScene::DrawModelLoadingPopup() {
  constexpr const char* kTitle = "##LoadingModels";
  const bool busy = IsModelPreloading();
  if (busy && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
  if (!ImGui::IsPopupOpen(kTitle)) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const auto& loader = GetModelPreloader();
  const size_t done = loader.Done();
  const size_t total = std::max<size_t>(1, loader.Total());
  if (ImGui::BeginPopupModal(kTitle, nullptr,
                             ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!busy) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::Text("Loading models... %zu / %zu", done, total);
      ImGui::ProgressBar(loader.Fraction(), ImVec2(360.0f, 0.0f));
      ImGui::TextDisabled("%s", loader.CurrentName().c_str());
    }
    ImGui::EndPopup();
  }
}

void EditorScene::DrawModals() {
  static const char* kTitles[] = {"", "New Script", "New Map", "New Folder",
                                  "Unsaved Changes", "Delete",
                                  "Build Game"};
  if (modal_ == Modal::kNone) return;
  const char* title = kTitles[static_cast<int>(modal_)];
  if (open_modal_) {
    ImGui::OpenPopup(title);
    open_modal_ = false;
    if (modal_ == Modal::kNewScript || modal_ == Modal::kNewMap ||
        modal_ == Modal::kNewFolder) {
      new_name_[0] = '\0';
    }
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(title, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    modal_ = Modal::kNone;
    return;
  }
  auto project = GetProject();
  auto close = [&] {
    modal_ = Modal::kNone;
    ImGui::CloseCurrentPopup();
  };

  switch (modal_) {
    case Modal::kNewScript: {
      ImGui::TextUnformatted("C++ class name for the new script:");
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool enter = ImGui::InputText("##name", new_name_, sizeof(new_name_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
      const std::string name = new_name_;
      const bool valid = Templates::IsValidIdentifier(name);
      if (!name.empty() && !valid) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Use letters, digits and _");
      } else if (valid) {
        ImGui::TextDisabled("Creates scripts/%s.h and .cc",
                            Templates::ToSnakeCase(name).c_str());
      }
      ImGui::BeginDisabled(!valid);
      if (ImGui::Button("Create") || (enter && valid)) {
        std::string error;
        auto file = Templates::CreateScript(project->GetRoot(), name, &error);
        if (file.empty()) {
          console_.Error("Could not create script: " + error);
        } else {
          console_.Info("Created " + file.string() +
                        ". Build to add it to the Add Component menu.");
          OpenInExternalEditor(file);
        }
        close();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) close();
      break;
    }
    case Modal::kNewMap:
    case Modal::kNewFolder: {
      const bool is_map = modal_ == Modal::kNewMap;
      ImGui::TextUnformatted(is_map ? "Map name:" : "Folder name:");
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool enter = ImGui::InputText("##name", new_name_, sizeof(new_name_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
      std::string name = new_name_;
      const bool valid = !name.empty() && name.find_first_of("/\\:") == std::string::npos;
      ImGui::BeginDisabled(!valid);
      if (ImGui::Button("Create") || (enter && valid)) {
        std::error_code ec;
        if (is_map) {
          if (!name.ends_with(kMapExtension)) name += kMapExtension;
          fs::create_directories(new_item_dir_, ec);
          const std::string res = project->ToResPath(new_item_dir_ / name);
          if (fs::exists(new_item_dir_ / name)) {
            console_.Error(res + " already exists");
          } else if (map_path_.empty()) {
            // First save of a map that was never written.
            map_path_ = res;
            SaveMap();
          } else {
            NewMap(res);
          }
        } else {
          fs::create_directories(new_item_dir_ / name, ec);
        }
        close();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) close();
      break;
    }
    case Modal::kUnsaved: {
      if (dirty_) ImGui::Text("%s has unsaved changes.", MapDisplayName().c_str());
      if (ui_builder_.IsDirty()) {
        ImGui::Text("%s has unsaved changes (UI Builder).",
                    fs::path(ui_builder_.path()).filename().string().c_str());
      }
      auto proceed = [&] {
        close();
        if (after_unsaved_ == AfterSave::kQuit) QuitScene();
        if (after_unsaved_ == AfterSave::kCloseProject) CloseProject();
      };
      if (ImGui::Button("Save")) {
        const bool map_ok = !dirty_ || SaveMap();
        const bool ui_ok = !ui_builder_.IsDirty() || ui_builder_.Save();
        if (map_ok && ui_ok) proceed();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
        dirty_ = false;
        ui_builder_.Close();
        proceed();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) close();
      break;
    }
    case Modal::kDelete: {
      ImGui::Text("Delete %s?", delete_target_.filename().string().c_str());
      ImGui::TextDisabled("This cannot be undone.");
      if (ImGui::Button("Delete")) {
        std::error_code ec;
        fs::remove_all(delete_target_, ec);
        if (ec) console_.Error("Could not delete: " + ec.message());
        selected_asset_.clear();
        close();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) close();
      break;
    }
    case Modal::kBuildGame: {
      ImGui::TextUnformatted("Output folder:");
      ImGui::SetNextItemWidth(420);
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      const bool enter = ImGui::InputText("##output", &build_output_,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
      const fs::path output = project->Resolve(build_output_);
      ImGui::TextDisabled("%s", output.string().c_str());
      ImGui::TextDisabled(
          "Writes the executable, engine.mpack and game.mpack there.");
      // Packing the project into its own root would pack the build too.
      std::error_code ec;
      const bool valid = !build_output_.empty() &&
                         !fs::equivalent(output, project->GetRoot(), ec);
      ImGui::BeginDisabled(!valid);
      if (ImGui::Button("Build") || (enter && valid)) {
        if (build_output_ != project->GetBuildOutput()) {
          project->SetBuildOutput(build_output_);
          if (!project->Save()) console_.Error("Could not save the project file");
        }
        if (SaveMap()) release_.Start(*project, output);
        close();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) close();
      break;
    }
    case Modal::kNone:
      break;
  }
  ImGui::EndPopup();
}

// --------------------------------------------------------------- Launcher ---

void EditorScene::DrawLauncher() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Always);
  ImGui::Begin("Mercury", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings);

  ImGui::SeparatorText("Recent projects");
  if (settings_.recent_projects.empty()) {
    ImGui::TextDisabled("No recent projects");
  }
  std::string to_open;
  std::string to_forget;
  for (const auto& path : settings_.recent_projects) {
    ImGui::PushID(path.c_str());
    const bool exists = fs::exists(path);
    const std::string label =
        fs::path(path).parent_path().filename().string() + "    " + path;
    ImGui::BeginDisabled(!exists);
    if (ImGui::Selectable(label.c_str(), false,
                          ImGuiSelectableFlags_AllowDoubleClick)) {
      to_open = path;
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Remove from list")) to_forget = path;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText("Open a project");
  ImGui::SetNextItemWidth(-90);
  const bool enter = ImGui::InputTextWithHint(
      "##open", "/path/to/game.mproj", open_path_, sizeof(open_path_),
      ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Open", ImVec2(-1, 0)) || enter) to_open = open_path_;

  ImGui::SeparatorText("New project");
  ImGui::SetNextItemWidth(200);
  ImGui::InputText("Name", new_project_name_, sizeof(new_project_name_));
  ImGui::SameLine();
  if (ImGui::RadioButton("2D", !new_project_3d_)) new_project_3d_ = false;
  ImGui::SameLine();
  if (ImGui::RadioButton("3D", new_project_3d_)) new_project_3d_ = true;
  ImGui::SetNextItemWidth(-90);
  ImGui::InputText("##location", new_project_dir_, sizeof(new_project_dir_));
  ImGui::SameLine();
  if (ImGui::Button("Create", ImVec2(-1, 0))) {
    std::string error;
    auto mproj = Templates::CreateProject(new_project_dir_, new_project_name_,
                                          new_project_3d_, &error);
    if (mproj.empty()) {
      console_.Error("Could not create project: " + error);
    } else {
      to_open = mproj.string();
    }
  }
  ImGui::TextDisabled("Creates <location>/<name>/ with a map, folders and a "
                      "CMakeLists.txt");

  // Errors from opening or creating appear here before a Console exists.
  auto lines = console_.Snapshot();
  for (size_t i = lines.size() > 3 ? lines.size() - 3 : 0; i < lines.size(); i++) {
    if (lines[i].level == Console::Level::kError) {
      ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", lines[i].text.c_str());
    }
  }
  ImGui::End();

  if (!to_forget.empty()) {
    std::erase(settings_.recent_projects, to_forget);
    settings_.Save();
  }
  if (!to_open.empty()) {
    fs::path path(to_open);
    // Accept a project folder as well as the .mproj itself.
    if (fs::is_directory(path)) {
      for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.path().extension() == kProjectExtension) {
          path = entry.path();
          break;
        }
      }
    }
    OpenProject(path);
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
  std::string base = type == "PhysicsMaterial" ? "physics_material"
                     : type == "PanelSettings" ? "panel_settings"
                                               : "material";
  fs::path file = dir / (base + std::string(kAssetExtension));
  for (int i = 2; fs::exists(file); i++) {
    file = dir / (base + std::to_string(i) + std::string(kAssetExtension));
  }
  if (Templates::CreateAsset(file, type)) {
    selected_asset_ = GetProject()->ToResPath(file);
    selected_uid_ = 0;
  } else {
    console_.Error("Could not create " + file.string());
  }
}

void EditorScene::OpenInUIBuilder(const std::string& res_path) {
  if (ui_builder_.IsOpen() && ui_builder_.path() == res_path) {
    settings_.show_ui_builder = true;
    ImGui::SetWindowFocus(UIBuilder::kWindowName);
    return;
  }
  if (ui_builder_.IsDirty()) ui_builder_.Save();
  if (ui_builder_.Open(res_path)) {
    if (!settings_.show_ui_builder) {
      settings_.show_ui_builder = true;
      settings_.Save();
    }
    ImGui::SetWindowFocus(UIBuilder::kWindowName);
  }
}

void EditorScene::CreateUIFile(const fs::path& dir, const std::string& kind) {
  const bool uxml = kind == "uxml";
  const std::string base = uxml ? "new_document" : "new_style";
  const std::string extension = uxml ? std::string(kUxmlExtension) : std::string(kUssExtension);
  fs::path file = dir / (base + extension);
  for (int i = 2; fs::exists(file); i++) {
    file = dir / (base + std::to_string(i) + extension);
  }
  const bool ok = uxml ? Templates::CreateUxml(file) : Templates::CreateUss(file);
  if (!ok) {
    console_.Error("Could not create " + file.string());
    return;
  }
  const std::string res_path = GetProject()->ToResPath(file);
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
  // Split the command on spaces (quotes group words), then fill in paths.
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
