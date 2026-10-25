/*
 * editor_scene.h
 *
 * The Mercury editor, modeled on Unity's: a project launcher, then a docked
 * workspace with Hierarchy, Inspector, Scene, Project and Console panels.
 *
 * In-game UI is edited in the UI Builder window (ui_builder.h), and the
 * project is versioned with Lore from the Lore panel (lore_vcs.h).
 *
 * The editor edits a Map (the data in a .mmap file) and mirrors it into an
 * edit-mode MapScene for drawing. Game code is never loaded here: Play builds
 * the project with CMake and runs the game as its own process.
 */
#ifndef MERCURY_EDITOR_EDITOR_SCENE_H
#define MERCURY_EDITOR_EDITOR_SCENE_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "editor_settings.h"
#include "imgui.h"
#include "lore_vcs.h"
#include "play_session.h"
#include "release_build.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "ui_builder.h"

namespace Mercury {

class EditorScene : public MapScene {
 public:
  explicit EditorScene(std::filesystem::path initial_project = {},
                       std::string initial_map = {});
  ~EditorScene() override;

  void OnStart() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;
  void OnDestroy() override;
  bool OnCloseRequested() override;

 private:
  // ---------------------------------------------------------------- project
  bool OpenProject(const std::filesystem::path& mproj);
  void CloseProject();
  bool OpenMap(const std::string& res_path);
  bool SaveMap();
  void NewMap(const std::string& res_path);
  void LoadScriptSchema();
  void UpdateWindowTitle();
  std::string MapDisplayName() const;

  // ------------------------------------------------------------- editing
  // Call after every change to map_. Changes made while one ImGui widget is
  // held (a drag, typing) become one undo step.
  // `rebuild_scene` false is for a change to one node's Transform, which
  // SetSelectedTransform hands to the scene alone (see transform_uid_).
  void MarkChanged(bool rebuild_scene = true);
  void EndFrameEdits();
  void Undo();
  void Redo();
  MapNode* GetSelectedNode();
  void Select(const MapNode* node);
  MapNode* CreateNode(MapNode* parent, const std::string& name);
  // Right-click > Import into Scene on a .obj: loads it in the background,
  // then adds a node for each of its objects and groups, each drawing just
  // that part of the model, so they can be moved or deleted one by one.
  void ImportModel(const std::string& res_path);
  void FinishModelImport();
  // Things the Create menus can add, like Unity's GameObject menu.
  enum class Preset {
    kEmpty, kCube, kSphere, kCapsule, kCylinder, kPlane, kQuad,
    kSprite, kDirectionalLight, kCamera, kPointLight, kAreaLight, kLightProbeVolume,
  };
  MapNode* CreatePreset(MapNode* parent, Preset preset);
  void DrawCreateMenuItems(MapNode* parent);
  void DeleteSelected();
  void DuplicateSelected();

  // -------------------------------------------------------------- panels
  void DrawLauncher();
  void DrawDockspace();
  void BuildDefaultLayout(ImGuiID dockspace);
  void DrawMenuBar();
  void DrawToolbar();
  void DrawHierarchy();
  void DrawHierarchyNode(MapNode& node);
  void DrawInspector();
  void DrawNodeInspector(MapNode& node);
  void DrawAssetInspector(const std::string& res_path);
  void DrawScene();
  void DrawProjectBrowser();
  void DrawProjectDirectory(const std::filesystem::path& dir);
  void DrawConsole();
  void DrawModals();
  void DrawModelLoadingPopup();
  void HandleShortcuts();

  // Draws the editor for one field; returns true if `properties` changed.
  bool DrawField(const FieldInfo& field, mtext::PropertyList& properties);
  // The widget for one value of `type` (the field's own type, or a list's
  // element type); true if it changed.
  bool DrawValue(const FieldInfo& field, FieldType type, mtext::Variant& value);
  bool DrawList(const FieldInfo& field, mtext::Variant& value);
  bool DrawEventCalls(mtext::Variant& value);
  bool DrawLockGroup(const char* label, const std::string& prefix,
                     mtext::PropertyList& properties);
  bool DrawRawProperty(mtext::Property& property);
  bool DrawAssetPicker(const char* label, std::string& path,
                       const std::string& asset_type);
  bool DrawNodePicker(const char* label, std::string& path);

  // ----------------------------------------------------------- scene view
  struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
  };
  enum class Tool { kMove, kRotate, kScale };
  // Whether move and rotate handles follow the node's rotation or the world
  // axes, like Unity's Local/Global toggle. Scale handles are always local.
  enum class HandleSpace { kLocal, kGlobal };
  // What a left-drag in the Scene view is changing.
  enum class Drag { kNone, kAxis, kPlane, kRotate, kScale, kUniformScale, kOrbit };

  void UpdateEditorCamera();
  glm::vec3 CameraForward() const;
  glm::vec3 CameraPosition() const;
  glm::mat4 GetViewProjection() const;
  Ray ScreenRay(ImVec2 pixel) const;
  // false if the point is behind the camera
  bool WorldToScreen(glm::vec3 world, ImVec2& pixel) const;
  // World units covered by one pixel at the depth of `point`
  float WorldPerPixel(glm::vec3 point) const;
  void DrawLine3D(ImDrawList* draw, glm::vec3 a, glm::vec3 b, ImU32 color,
                  float thickness = 1.0f) const;
  void DrawCircle3D(ImDrawList* draw, glm::vec3 center, glm::vec3 axis,
                    float radius, ImU32 color, float thickness = 1.0f) const;
  void DrawBox3D(ImDrawList* draw, const glm::mat4& model, glm::vec3 min,
                 glm::vec3 max, ImU32 color, float thickness = 1.0f) const;
  void DrawGrid(ImDrawList* draw);
  // The gizmos of the node `entity` stands for (its NodeInfo `info`).
  void DrawNodeGizmos(ImDrawList* draw, kEntityId entity, const NodeInfo& info);
  // Every node's gizmos: only those with something to show are visited.
  void DrawAllGizmos(ImDrawList* draw);
  void DrawToolHandles(ImDrawList* draw);
  void DrawSceneToolbar();
  void DrawAxisIndicator(ImDrawList* draw);
  // When a camera is selected, a small view of what it sees in the bottom
  // right corner of the Scene panel.
  void DrawCameraPreview(ImDrawList* draw);
  void HandleSceneCamera(bool hovered, bool active);
  // Starts a handle drag if the mouse is over one of the selection's handles.
  bool BeginHandleDrag(ImVec2 mouse);
  void UpdateDrag(ImVec2 mouse);
  // The rotation of the move and rotate handles of `transform`'s node.
  glm::quat HandleRotation(const Transform& transform) const;
  // A world-space move of the dragged node in its parent's space.
  glm::vec3 WorldDeltaToLocal(glm::vec3 delta) const;
  void SetSelectedTransform(const char* key, glm::vec3 value);
  MapNode* PickNode(ImVec2 pixel);
  // Model matrix (position, rotation, scale) and local bounds of what the
  // node draws or collides with, for picking and outlines.
  bool GetNodeBounds(kEntityId entity, glm::mat4& model, glm::vec3& min,
                     glm::vec3& max);
  glm::mat4 GetNodeMatrix(kEntityId entity, bool with_scale = true);
  void FrameSelected();
  void FrameMap();
  // Guess from the map's contents whether the 2D or 3D view suits it.
  bool MapLooks3D() const;

  // -------------------------------------------------------------- assets
  std::vector<std::string> ListProjectFiles(
      const std::vector<std::string>& extensions) const;
  void CreateAsset(const std::filesystem::path& dir, const std::string& type);
  void OpenInExternalEditor(const std::filesystem::path& file);
  void OpenInUIBuilder(const std::string& res_path);
  // A new file in `dir` from `make` (name, extension); selects it.
  void CreateUIFile(const std::filesystem::path& dir, const std::string& kind);

  std::filesystem::path initial_project_;
  std::string initial_map_;  // opened instead of the main map
  EditorSettings settings_;
  Console console_;
  PlaySession play_;
  ReleaseBuild release_{console_};
  UIBuilder ui_builder_{console_};
  LoreVcs lore_{console_, settings_};

  // The open project is MapScene::GetProject().
  Map map_;
  std::string map_path_;  // res:// path of the open map
  bool dirty_ = false;
  bool scene_dirty_ = true;
  // The node whose Transform changed since the scene last took it, when
  // nothing else needs the scene rebuilt: dragging a handle moves one
  // entity instead of recreating all of them every frame.
  uint64_t transform_uid_ = 0;

  // Undo keeps whole-map snapshots; maps are small text documents.
  std::vector<Map> undo_;
  std::vector<Map> redo_;
  Map last_committed_;
  bool edit_open_ = false;

  uint64_t selected_uid_ = 0;
  std::string selected_asset_;  // res:// path shown in the Inspector
  uint64_t rename_uid_ = 0;     // node being renamed in the Hierarchy
  // An edit changed the map while the Hierarchy was drawing it: the rest
  // of the tree waits for the next frame.
  bool tree_changed_ = false;
  std::string pending_import_;  // the model ImportModel waits to have loaded

  // Scene view state. The editor camera looks at camera_target_ from
  // camera_distance_ away; in 2D it looks straight down -Z.
  bool view_2d_ = true;
  Tool tool_ = Tool::kMove;
  HandleSpace handle_space_ = HandleSpace::kLocal;
  glm::vec3 camera_target_ = glm::vec3(0.0f);
  float camera_distance_ = 120.0f;
  float camera_yaw_ = 35.0f;     // degrees, 3D only
  float camera_pitch_ = -25.0f;  // degrees, 3D only
  ImVec2 scene_origin_ = {0, 0};
  ImVec2 scene_size_ = {1, 1};
  // Screen rect of the camera preview from the last frame (min, max); empty
  // when hidden. Clicks there do not reach the scene.
  ImVec2 preview_min_ = {0, 0};
  ImVec2 preview_max_ = {0, 0};
  Drag drag_ = Drag::kNone;
  int drag_axis_ = 0;
  glm::vec3 drag_axis_dir_ = glm::vec3(1, 0, 0);
  glm::vec3 drag_origin_ = glm::vec3(0.0f);  // the node's world position
  glm::vec3 drag_plane_normal_ = glm::vec3(0, 0, 1);
  glm::vec3 drag_start_world_ = glm::vec3(0.0f);
  glm::vec3 drag_start_local_ = glm::vec3(0.0f);
  glm::vec3 drag_start_rotation_ = glm::vec3(0.0f);
  glm::vec3 drag_start_scale_ = glm::vec3(1.0f);
  // The parent's world rotation and scale, to turn world moves into the
  // node's local position and rotation.
  glm::quat drag_parent_rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 drag_parent_scale_ = glm::vec3(1.0f);
  glm::quat drag_handle_rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  float drag_start_param_ = 0.0f;
  float drag_length_ = 1.0f;  // world length of the dragged handle
  float drag_accum_ = 0.0f;   // radians turned so far by a rotate drag
  ImVec2 drag_start_mouse_ = {0, 0};
  int hovered_handle_ = -1;  // highlighted handle under the mouse
  float game_aspect_ = 16.0f / 9.0f;

  // Launcher / dialogs
  char open_path_[1024] = "";
  char new_project_name_[128] = "MyGame";
  bool new_project_3d_ = false;
  char new_project_dir_[1024] = "";
  char new_name_[128] = "";
  std::filesystem::path new_item_dir_;
  enum class Modal {
    kNone,
    kNewScript,
    kNewMap,
    kNewFolder,
    kUnsaved,
    kDelete,
    kBuildGame
  };
  Modal modal_ = Modal::kNone;
  bool open_modal_ = false;
  std::filesystem::path delete_target_;
  std::string build_output_;  // Build Game's output folder, as typed
  enum class AfterSave { kNothing, kQuit, kCloseProject };
  bool close_project_requested_ = false;
  AfterSave after_unsaved_ = AfterSave::kNothing;

  bool reset_layout_ = false;
  double project_scan_timer_ = 0.0;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_EDITOR_SCENE_H
