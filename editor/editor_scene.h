/*
 * editor_scene.h
 *
 * The Mercury editor, modeled on Unity's: a project launcher, then a docked
 * workspace with Hierarchy, Inspector, Scene, Project and Console panels.
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
#include "play_session.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"

namespace Mercury {

class EditorScene : public MapScene {
 public:
  explicit EditorScene(std::filesystem::path initial_project = {});
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
  void MarkChanged();
  void EndFrameEdits();
  void Undo();
  void Redo();
  MapNode* GetSelectedNode();
  void Select(const MapNode* node);
  MapNode* CreateNode(MapNode* parent, const std::string& name);
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
  void DrawSceneGizmos(ImDrawList* draw, ImVec2 origin, ImVec2 size);
  void DrawProjectBrowser();
  void DrawProjectDirectory(const std::filesystem::path& dir);
  void DrawConsole();
  void DrawModals();
  void HandleShortcuts();

  // Draws the editor for one field; returns true if `properties` changed.
  bool DrawField(const FieldInfo& field, mtext::PropertyList& properties);
  bool DrawRawProperty(mtext::Property& property);
  bool DrawAssetPicker(const char* label, std::string& path,
                       const std::string& asset_type);
  bool DrawNodePicker(const char* label, std::string& path);

  // ----------------------------------------------------------- scene view
  void UpdateEditorCamera();
  glm::mat4 GetViewProjection() const;
  std::optional<glm::vec3> ScreenToWorld(ImVec2 pixel) const;  // on z = 0
  ImVec2 WorldToScreen(glm::vec3 world) const;
  MapNode* PickNode(glm::vec3 world_point);
  // World-space bounds used for picking and the selection outline.
  bool GetNodeBounds(kEntityId entity, glm::vec3& min, glm::vec3& max);

  // -------------------------------------------------------------- assets
  std::vector<std::string> ListProjectFiles(
      const std::vector<std::string>& extensions) const;
  void CreateAsset(const std::filesystem::path& dir, const std::string& type);
  void OpenInExternalEditor(const std::filesystem::path& file);

  std::filesystem::path initial_project_;
  EditorSettings settings_;
  Console console_;
  PlaySession play_;

  // The open project is MapScene::GetProject().
  Map map_;
  std::string map_path_;  // res:// path of the open map
  bool dirty_ = false;
  bool scene_dirty_ = true;

  // Undo keeps whole-map snapshots; maps are small text documents.
  std::vector<Map> undo_;
  std::vector<Map> redo_;
  Map last_committed_;
  bool edit_open_ = false;

  uint64_t selected_uid_ = 0;
  std::string selected_asset_;  // res:// path shown in the Inspector
  uint64_t rename_uid_ = 0;     // node being renamed in the Hierarchy

  // Scene view state
  glm::vec3 camera_target_ = glm::vec3(0.0f);
  float camera_distance_ = 120.0f;
  ImVec2 scene_origin_ = {0, 0};
  ImVec2 scene_size_ = {1, 1};
  bool dragging_node_ = false;
  glm::vec3 drag_start_world_ = glm::vec3(0.0f);
  glm::vec3 drag_start_local_ = glm::vec3(0.0f);
  float game_aspect_ = 16.0f / 9.0f;

  // Launcher / dialogs
  char open_path_[1024] = "";
  char new_project_name_[128] = "MyGame";
  char new_project_dir_[1024] = "";
  char new_name_[128] = "";
  std::filesystem::path new_item_dir_;
  enum class Modal { kNone, kNewScript, kNewMap, kNewFolder, kUnsaved, kDelete };
  Modal modal_ = Modal::kNone;
  bool open_modal_ = false;
  std::filesystem::path delete_target_;
  enum class AfterSave { kNothing, kQuit, kCloseProject };
  AfterSave after_unsaved_ = AfterSave::kNothing;

  bool reset_layout_ = false;
  double project_scan_timer_ = 0.0;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_EDITOR_SCENE_H
