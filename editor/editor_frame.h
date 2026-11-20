// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
#include <wx/notebook.h>
#include <wx/wx.h>

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "console_panel.h"
#include "editor_scene.h"
#include "extension.h"
#include "hierarchy_panel.h"
#include "input_actions_panel.h"
#include "inspector_panel.h"
#include "lore_panel.h"
#include "project_browser_panel.h"
#include "scene_viewport_panel.h"
#include "ui_builder_panel.h"

namespace Mercury::EditorUI {

class EditorFrame;
class ScriptEditorPanel;

class MercuryAuiManager : public wxAuiManager {
 public:
  MercuryAuiManager(EditorFrame* owner, unsigned int flags);

  wxAuiFloatingFrame* CreateFloatingFrame(wxWindow* parent,
                                          const wxAuiPaneInfo& p) override;

 private:
  EditorFrame* owner_ = nullptr;
};

/**
 * @brief The main editor window: menus, toolbar, docked panels and tabs
 *
 * EditorFrame owns the wxAUI layout and the panels (Hierarchy, Inspector,
 * Scene Viewport, Project Browser, Console, Script Editor, UI Builder, Input
 * Actions, Lore VCS). It forwards menu and toolbar commands to
 * @ref Mercury::EditorScene, which is the model; the frame is the view.
 *
 * Panels are synced from the controller either every 60 fps UI tick
 * (`SyncPanels`) or at the viewport's higher rate (`SyncViewports`). Closing a
 * project returns to the launcher; closing the frame exits the app.
 */
class EditorFrame : public wxFrame {
 public:
  EditorFrame(EditorScene& scene, std::function<void()> on_close_project,
              std::function<void()> on_exit_app);
  ~EditorFrame() override;

  void SyncFromController(float dt);
  // Split so the app can drive the Vulkan viewport canvas from its own
  // unlocked loop while the rest of the editor UI stays at 60fps.
  void SyncPanels(float dt);
  void SyncViewports(float dt);
  // True while the scene viewport is being navigated/dragged, so the app
  // should render it at an unlocked frame rate.
  bool WantsUnlockedViewportFps() const;
  void SaveLayoutPerspective();
  bool ConfirmDiscardOrSave(const wxString& action_prompt,
                            bool include_scripts = false);
  void DestroyChildFrames();
  void DockPane(const wxString& pane_name);
  void DockAllFloatingPanels();
  void NotifyGlobalKeyUp(wxKeyEvent& evt);

  // --- extensions (editor/extension.h) -----------------------------------
  //
  // Applies the panels and menu items loaded project systems registered, and
  // removes one module's UI before its library is unloaded. A reload calls
  // RemoveExtensionUI, unloads, then ApplyExtensions.
  void ApplyExtensions();
  void RemoveExtensionUI(std::string_view module);
  void SetOnReloadSystems(std::function<void()> callback) {
    on_reload_systems_ = std::move(callback);
  }
  void SetOnTrustSystems(std::function<void()> callback) {
    on_trust_systems_ = std::move(callback);
  }
  wxAuiManager& GetAuiManager() { return aui_; }

 private:
  void BuildMenuBar();
  void BuildToolbar();
  void BuildAuiPanes();
  void SetupMenuBindings();
  void RefreshToolbarAndStatus();
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
  void ShowScriptEditor();
  void ShowSceneView();
  void OpenPendingScript();

  void OnCharHook(wxKeyEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void OnAuiPaneClose(wxAuiManagerEvent& evt);
  void OnAuiPaneButton(wxAuiManagerEvent& evt);

  // Extension UI bookkeeping.
  void OnExtensionMenu(wxCommandEvent& evt);
  void ApplyExtensionMenuItem(std::string module, const std::string& menu,
                              const std::string& label,
                              const std::string& accelerator,
                              std::function<void()> callback);
  void ToggleExtensionPane(int toggle_id);
  int NextExtensionMenuId();
  void ReleaseExtensionMenuId(int id);

  struct ExtensionPane {
    std::string module;
    std::string id;
    wxWindow* window = nullptr;
    int toggle_id = 0;
    wxMenuItem* menu_item = nullptr;
  };
  struct ExtensionMenuEntry {
    std::string module;
    int id = 0;
    wxMenu* menu = nullptr;
  };
  std::vector<ExtensionPane> extension_panes_;
  std::vector<ExtensionMenuEntry> extension_menu_items_;
  std::vector<wxMenu*> extension_owned_menus_;
  std::unordered_map<int, std::function<void()>> extension_callbacks_;
  EditorMenuIdPool extension_ids_;
  std::function<void()> on_reload_systems_;
  std::function<void()> on_trust_systems_;

  EditorScene& scene_;
  std::function<void()> on_close_project_;
  std::function<void()> on_exit_app_;

  MercuryAuiManager aui_;
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
  bool toolbar_synced_once_ = false;
  PlaySession::State last_play_state_ = PlaySession::State::kIdle;
  bool status_synced_once_ = false;
  bool last_play_enabled_ = false;
  bool last_build_enabled_ = false;
  bool last_save_enabled_ = false;
  bool last_undo_enabled_ = false;
  bool last_redo_enabled_ = false;
  std::string last_map_label_;
  std::string last_status_hint_;
  std::string last_status_proj_;

  // Docked panels
  wxNotebook* center_tabs_ = nullptr;
  ScriptEditorPanel* script_editor_panel_ = nullptr;
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
  wxMenuItem* mi_script_ = nullptr;
  wxMenuItem* mi_inspector_ = nullptr;
  wxMenuItem* mi_project_ = nullptr;
  wxMenuItem* mi_console_ = nullptr;
  wxMenuItem* mi_lore_ = nullptr;
  wxMenuItem* mi_uibuilder_ = nullptr;
  wxMenuItem* mi_input_actions_ = nullptr;
  wxMenuItem* mi_cam_preview_ = nullptr;

  int last_game_fps_ = -1;
  std::chrono::steady_clock::time_point last_fps_update_{};
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_EDITOR_FRAME_H
