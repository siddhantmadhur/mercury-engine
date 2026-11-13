/*
 * editor_scene.h
 *
 * Framework-independent controller & edit-mode MapScene for the Mercury Editor.
 * Manages project/map loading, undo/redo history, scene/entity synchronization,
 * 3D/2D scene viewport camera & gizmo math, model preloading, script builds,
 * Play/Release sessions, UIBuilder, InputActionsWindow, and LoreVcs.
 */
#ifndef MERCURY_EDITOR_EDITOR_SCENE_H
#define MERCURY_EDITOR_EDITOR_SCENE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "console.h"
#include "editor_settings.h"
#include "input_actions_window.h"
#include "lore_vcs.h"
#include "overlay_canvas.h"
#include "play_session.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "release_build.h"
#include "scene/map_scene.h"
#include "ui_builder.h"

namespace Mercury {

class EditorScene : public MapScene {
 public:
  enum class Preset {
    kEmpty,
    kCube,
    kSphere,
    kCapsule,
    kCylinder,
    kPlane,
    kQuad,
    kSprite,
    kDirectionalLight,
    kCamera,
    kPointLight,
    kAreaLight,
    kLightProbeVolume,
  };

  enum class Tool { kMove, kRotate, kScale };
  // Whether move and rotate handles follow the node's rotation or the world
  // axes. Scale handles are always local.
  enum class HandleSpace { kLocal, kGlobal };
  // What a left-drag in the Scene view is changing.
  enum class Drag {
    kNone,
    kAxis,
    kPlane,
    kRotate,
    kScale,
    kUniformScale,
    kOrbit,
  };

  enum WasdqeBits {
    kFlyW = 1 << 0,
    kFlyS = 1 << 1,
    kFlyD = 1 << 2,
    kFlyA = 1 << 3,
    kFlyE = 1 << 4,
    kFlyQ = 1 << 5,
    kNavUp = 1 << 6,
    kNavDown = 1 << 7,
    kNavLeft = 1 << 8,
    kNavRight = 1 << 9,
  };

  struct PackSizeOption {
    const char* label;
    int megabytes;  // 0 = single archive
  };
  static constexpr size_t kNumPackSizes = 6;
  static const PackSizeOption kPackSizes[kNumPackSizes];

  struct CameraPreviewInfo {
    kEntityId entity = entt::null;
    int width = 0;
    int height = 0;
    std::string label;
  };

  explicit EditorScene(std::filesystem::path initial_project = {},
                       std::string initial_map = {});
  ~EditorScene() override;

  // MapScene lifecycle
  void OnStart() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;
  void OnDestroy() override;
  bool OnCloseRequested() override;

  // Engine accessors
  RenderHost* GetRenderHost() const { return render_host_; }
  InputManager* GetInputManager() { return Input(); }

  // Project / Map management
  bool OpenProject(const std::filesystem::path& mproj);
  // Accepts either a .mproj file or a folder containing one.
  bool OpenProjectOrDirectory(const std::filesystem::path& path);
  void CloseProject();
  bool HasUnsavedChanges() const;
  void DiscardAllChanges();
  bool SaveAllDirty();
  bool OpenMap(const std::string& res_path);
  bool SaveMap();
  bool SaveNewMapAs(const std::string& res_path);
  void NewMap(const std::string& res_path);
  bool SetMainMap();
  void LoadScriptSchema();
  std::string MapDisplayName() const;
  std::string WindowTitle() const;
  const std::string& GetMapPath() const { return map_path_; }
  bool IsDirty() const { return dirty_; }
  Map& GetMap() { return map_; }
  const Map& GetMap() const { return map_; }

  // Undo/redo & change coalescing
  void BeginEdit();
  void CommitEdit();
  // `rebuild_scene` false is for a change to one node's Transform, which
  // moves only that entity instead of rebuilding all entities.
  // `coalesced` keeps the current undo step open until CommitEdit().
  void MarkChanged(bool rebuild_scene = true, bool coalesced = false);
  void NotifyTransformEdited(uint64_t uid, bool coalesced = false);
  void Undo();
  void Redo();
  bool CanUndo() const { return !undo_.empty() || edit_open_; }
  bool CanRedo() const { return !redo_.empty(); }

  // Monotonic revision counters for retained UI updates
  uint64_t MapRevision() const { return map_revision_; }
  uint64_t SelectionRevision() const { return selection_revision_; }
  uint64_t SchemaRevision() const { return schema_revision_; }
  // Changes only when nodes are added, removed, renamed or reparented. The
  // hierarchy panel uses this instead of MapRevision so moving a node's
  // transform (which bumps MapRevision every drag tick) does not rebuild the
  // whole tree.
  uint64_t HierarchyRevision() const { return hierarchy_revision_; }
  // Bumped whenever the rendered scene/overlay view changes (camera, tools,
  // selection, scene content) so the viewport repaints only when needed.
  uint64_t ViewRevision() const { return view_revision_; }
  // True while the scene may change without an edit (running scripts or an
  // in-flight model load), so the viewport must keep repainting.
  bool NeedsContinuousRedraw() const;
  void RequestViewRedraw();

  // Selection & Hierarchy editing
  MapNode* GetSelectedNode();
  const MapNode* GetSelectedNode() const;
  uint64_t GetSelectedUid() const { return selected_uid_; }
  void Select(const MapNode* node);
  void SelectAsset(const std::string& res_path);
  const std::string& GetSelectedAsset() const { return selected_asset_; }
  void RequestRename(uint64_t uid) { rename_uid_ = uid; }
  uint64_t ConsumeRenameRequest();

  MapNode* CreateNode(MapNode* parent, const std::string& name);
  MapNode* CreatePreset(MapNode* parent, Preset preset);
  bool RenameNode(MapNode* node, const std::string& name);
  bool ReparentNode(MapNode* node, MapNode* new_parent, int index = -1);
  void DeleteSelected();
  void DuplicateSelected();
  bool AddComponent(MapNode* node, const std::string& type);
  bool RemoveComponent(MapNode* node, size_t index);
  bool MoveComponent(MapNode* node, size_t from, size_t to);
  bool ResetComponentToDefaults(MapNode* node, size_t index);
  bool SaveAsset(Asset& asset);

  void ImportModel(const std::string& res_path);
  std::string PendingImportResPath() const { return pending_import_; }

  // Project files & external tools
  std::vector<std::string> ListProjectFiles(
      const std::vector<std::string>& extensions) const;
  void CreateAsset(const std::filesystem::path& dir, const std::string& type);
  bool CreateScript(const std::string& class_name);
  bool CreateFolder(const std::filesystem::path& dir, const std::string& name);
  bool DeleteProjectPath(const std::filesystem::path& target);
  void OpenInExternalEditor(const std::filesystem::path& file);
  void OpenInUIBuilder(const std::string& res_path);
  void OpenInInputActions(const std::string& res_path);
  bool ConsumeUIBuilderFocusRequest();
  bool ConsumeInputActionsFocusRequest();
  void CreateInputActionsFile(const std::filesystem::path& dir);
  void CreateUIFile(const std::filesystem::path& dir, const std::string& kind);
  bool BuildGame(const std::string& output_str);

  // Subsystem accessors
  EditorSettings& Settings() { return settings_; }
  const EditorSettings& Settings() const { return settings_; }
  Console& ConsoleRef() { return console_; }
  PlaySession& PlayRef() { return play_; }
  ReleaseBuild& ReleaseRef() { return release_; }
  UIBuilder& UIBuilderRef() { return ui_builder_; }
  InputActionsWindow& InputActionsRef() { return input_actions_; }
  LoreVcs& LoreRef() { return lore_; }

  // Scene Viewport & Camera API
  void SetSceneViewportSize(glm::vec2 size);
  glm::vec2 GetSceneViewportSize() const { return scene_size_; }
  Tool GetTool() const { return tool_; }
  void SetTool(Tool tool) {
    if (tool_ == tool) return;
    tool_ = tool;
    RequestViewRedraw();
  }
  HandleSpace GetHandleSpace() const { return handle_space_; }
  void SetHandleSpace(HandleSpace space) {
    if (handle_space_ == space) return;
    handle_space_ = space;
    RequestViewRedraw();
  }
  void ToggleHandleSpace();
  bool IsView2D() const { return view_2d_; }
  void SetView2D(bool view_2d);
  void ToggleView2D() { SetView2D(!view_2d_); }
  const glm::vec3& GetCameraTarget() const { return camera_target_; }
  glm::vec3 GetCameraPosition() const { return CameraPosition(); }
  const char* GetHintText() const;
  void FrameSelected();
  void FrameMap();

  void RenderOverlays(OverlayCanvas& canvas, glm::vec2 local_mouse,
                      bool hovered, bool alt_down, bool right_down);
  CameraPreviewInfo UpdatePreviewCamera(int override_width = 0,
                                        int override_height = 0);

  void OnViewportWheel(float wheel_steps, glm::vec2 local_mouse);
  void UpdateViewportNavigation(glm::vec2 mouse_delta, bool left_down,
                                bool right_down, bool middle_down,
                                int wasdqe_mask, bool shift_down, float dt);
  void OnViewportMouseDown(glm::vec2 local_mouse, int button, bool alt_down);
  void OnViewportMouseDrag(glm::vec2 local_mouse, bool ctrl_snap);
  void OnViewportMouseUp();
  void UpdateHoveredHandle(glm::vec2 local_mouse, bool hovered, bool alt_down,
                           bool right_down);
  bool IsDragging() const { return drag_ != Drag::kNone; }
  bool IsDraggingHandle() const {
    return drag_ != Drag::kNone && drag_ != Drag::kOrbit;
  }

 private:
  struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
  };

  void FinishModelImport();

  // Scene view math & overlay drawing
  void UpdateEditorCamera();
  glm::vec3 CameraForward() const;
  glm::vec3 CameraPosition() const;
  glm::mat4 GetViewProjection() const;
  Ray ScreenRay(glm::vec2 pixel) const;
  bool WorldToScreen(glm::vec3 world, glm::vec2& pixel) const;
  float WorldPerPixel(glm::vec3 point) const;
  void DrawLine3D(OverlayCanvas& draw, glm::vec3 a, glm::vec3 b,
                  OverlayColor color, float thickness = 1.0f) const;
  void DrawLine3D(OverlayCanvas& draw, const glm::mat4& view_projection,
                  glm::vec3 a, glm::vec3 b, OverlayColor color,
                  float thickness = 1.0f) const;
  void DrawCircle3D(OverlayCanvas& draw, glm::vec3 center, glm::vec3 axis,
                    float radius, OverlayColor color,
                    float thickness = 1.0f) const;
  void DrawBox3D(OverlayCanvas& draw, const glm::mat4& model, glm::vec3 min,
                 glm::vec3 max, OverlayColor color,
                 float thickness = 1.0f) const;
  void DrawGrid(OverlayCanvas& draw);
  void DrawNodeGizmos(OverlayCanvas& draw, kEntityId entity,
                      const NodeInfo& info);
  void DrawAllGizmos(OverlayCanvas& draw);
  void DrawToolHandles(OverlayCanvas& draw);
  void DrawAxisIndicator(OverlayCanvas& draw);
  bool BeginHandleDrag(glm::vec2 mouse);
  void UpdateDrag(glm::vec2 mouse, bool snap);
  glm::quat HandleRotation(const Transform& transform) const;
  glm::vec3 WorldDeltaToLocal(glm::vec3 delta) const;
  void SetSelectedTransform(const char* key, glm::vec3 value, bool coalesced);
  MapNode* PickNode(glm::vec2 pixel);
  bool GetNodeBounds(kEntityId entity, glm::mat4& model, glm::vec3& min,
                     glm::vec3& max);
  glm::mat4 GetNodeMatrix(kEntityId entity, bool with_scale = true);
  bool MapLooks3D() const;

  std::filesystem::path initial_project_;
  std::string initial_map_;
  EditorSettings settings_;
  Console console_;
  PlaySession play_;
  ReleaseBuild release_{console_};
  UIBuilder ui_builder_{console_};
  InputActionsWindow input_actions_{console_};
  LoreVcs lore_{console_, settings_};

  Map map_;
  std::string map_path_;
  bool dirty_ = false;
  bool scene_dirty_ = true;
  uint64_t transform_uid_ = 0;

  std::vector<Map> undo_;
  std::vector<Map> redo_;
  Map last_committed_;
  bool edit_open_ = false;
  bool coalesced_session_ = false;

  uint64_t map_revision_ = 1;
  uint64_t selection_revision_ = 1;
  uint64_t schema_revision_ = 1;
  uint64_t hierarchy_revision_ = 1;
  uint64_t view_revision_ = 1;
  // The editor camera transform only needs syncing into the scene after the
  // view state or the scene itself changed.
  bool editor_camera_dirty_ = true;

  uint64_t selected_uid_ = 0;
  std::string selected_asset_;
  uint64_t rename_uid_ = 0;
  std::string pending_import_;

  bool focus_ui_builder_ = false;
  bool focus_input_actions_ = false;

  // Scene view state
  bool view_2d_ = true;
  Tool tool_ = Tool::kMove;
  HandleSpace handle_space_ = HandleSpace::kLocal;
  glm::vec3 camera_target_ = glm::vec3(0.0f);
  float camera_distance_ = 120.0f;
  float camera_yaw_ = 35.0f;
  float camera_pitch_ = -25.0f;
  glm::vec2 scene_size_ = {1.0f, 1.0f};
  Drag drag_ = Drag::kNone;
  int drag_axis_ = 0;
  glm::vec3 drag_axis_dir_ = glm::vec3(1, 0, 0);
  glm::vec3 drag_origin_ = glm::vec3(0.0f);
  glm::vec3 drag_plane_normal_ = glm::vec3(0, 0, 1);
  glm::vec3 drag_start_world_ = glm::vec3(0.0f);
  glm::vec3 drag_start_local_ = glm::vec3(0.0f);
  glm::vec3 drag_start_rotation_ = glm::vec3(0.0f);
  glm::vec3 drag_start_scale_ = glm::vec3(1.0f);
  glm::quat drag_parent_rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  glm::vec3 drag_parent_scale_ = glm::vec3(1.0f);
  glm::quat drag_handle_rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  float drag_start_param_ = 0.0f;
  float drag_length_ = 1.0f;
  float drag_accum_ = 0.0f;
  glm::vec2 drag_start_mouse_ = {0.0f, 0.0f};
  int hovered_handle_ = -1;
  float game_aspect_ = 16.0f / 9.0f;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_EDITOR_SCENE_H
