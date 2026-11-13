/*
 * editor_frame.h
 *
 * Main retained-mode Editor OS window with wxAuiManager docking layout,
 * native wxMenuBar, Source 2 / Glacier styled toolbar, persistent layout
 * perspective, global shortcuts, unsaved-changes guard, and detachable
 * native frames (CameraPreviewFrame, UIBuilderFrame, InputActionsFrame).
 */
#ifndef MERCURY_EDITOR_EDITOR_FRAME_H
#define MERCURY_EDITOR_EDITOR_FRAME_H

#include <wx/aui/aui.h>
#include <wx/wx.h>

#include <functional>
#include <string>

#include "console_panel.h"
#include "editor_scene.h"
#include "hierarchy_panel.h"
#include "input_actions_panel.h"
#include "inspector_panel.h"
#include "lore_panel.h"
#include "project_browser_panel.h"
#include "scene_viewport_panel.h"
#include "ui_builder_panel.h"

namespace Mercury::EditorUI {

class EditorFrame : public wxFrame {
 public:
  EditorFrame(EditorScene& scene, std::function<void()> on_close_project,
              std::function<void()> on_exit_app);
  ~EditorFrame() override;

  void SyncFromController(float dt);
  void SaveLayoutPerspective();
  bool ConfirmDiscardOrSave(const wxString& action_prompt);
  void DestroyChildFrames();

 private:
  void BuildMenuBar();
  void BuildToolbar();
  void BuildAuiPanes();
  void SetupMenuBindings();
  void RefreshToolbarAndStatus(float dt);
  void SyncWindowMenuChecks();

  void TogglePane(const char* pane_name, bool show);
  void ShowCameraPreviewPopout(bool show);
  void ShowUIBuilderPopout(bool show);
  void ShowInputActionsPopout(bool show);
  void RevealUIBuilder();
  void RevealInputActions();
  void HandleTogglePlay();
  void HandleNewMap();
  void HandleOpenMap();
  void HandleSaveMapAs();

  void OnCharHook(wxKeyEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void OnAuiPaneClose(wxAuiManagerEvent& evt);

  EditorScene& scene_;
  std::function<void()> on_close_project_;
  std::function<void()> on_exit_app_;

  wxAuiManager aui_;
  wxString default_perspective_;
  bool closing_ = false;

  // Toolbar widgets
  wxPanel* toolbar_ = nullptr;
  wxButton* play_btn_ = nullptr;
  wxButton* build_scripts_btn_ = nullptr;
  wxButton* build_game_btn_ = nullptr;
  wxButton* save_btn_ = nullptr;
  wxButton* undo_btn_ = nullptr;
  wxButton* redo_btn_ = nullptr;
  wxStaticText* map_status_lbl_ = nullptr;
  wxStaticText* fps_lbl_ = nullptr;

  // Docked panels
  HierarchyPanel* hierarchy_panel_ = nullptr;
  SceneViewportPanel* scene_panel_ = nullptr;
  InspectorPanel* inspector_panel_ = nullptr;
  ProjectBrowserPanel* project_panel_ = nullptr;
  ConsolePanel* console_panel_ = nullptr;
  LorePanel* lore_panel_ = nullptr;
  UIBuilderPanel* ui_builder_panel_ = nullptr;
  InputActionsPanel* input_actions_panel_ = nullptr;

  // Pop-out OS frames
  CameraPreviewFrame* camera_preview_frame_ = nullptr;
  UIBuilderFrame* ui_builder_frame_ = nullptr;
  InputActionsFrame* input_actions_frame_ = nullptr;

  // Window menu check items
  wxMenu* window_menu_ = nullptr;
  wxMenuItem* mi_hierarchy_ = nullptr;
  wxMenuItem* mi_scene_ = nullptr;
  wxMenuItem* mi_inspector_ = nullptr;
  wxMenuItem* mi_project_ = nullptr;
  wxMenuItem* mi_console_ = nullptr;
  wxMenuItem* mi_lore_ = nullptr;
  wxMenuItem* mi_uibuilder_ = nullptr;
  wxMenuItem* mi_input_actions_ = nullptr;
  wxMenuItem* mi_cam_preview_ = nullptr;

  float fps_accum_ = 0.0f;
  int fps_frames_ = 0;
  float fps_display_ = 0.0f;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_EDITOR_FRAME_H
