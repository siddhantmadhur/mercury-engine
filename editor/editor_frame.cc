#include "editor_frame.h"

#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/statline.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "create_menu.h"
#include "dialogs.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

enum MenuId {
  kIdFileNewMap = wxID_HIGHEST + 100,
  kIdFileOpenMap,
  kIdFileSave,
  kIdFileSaveAs,
  kIdFileSetMainMap,
  kIdFileBuildGame,
  kIdFilePreferences,
  kIdFileCloseProject,
  kIdFileExit,

  kIdEditUndo,
  kIdEditRedo,
  kIdEditDuplicate,
  kIdEditDelete,

  kIdProjectBuildScripts,
  kIdProjectTogglePlay,

  kIdWinHierarchy,
  kIdWinScene,
  kIdWinInspector,
  kIdWinProject,
  kIdWinConsole,
  kIdWinLore,
  kIdWinUIBuilder,
  kIdWinInputActions,
  kIdWinPopoutUIBuilder,
  kIdWinPopoutInputActions,
  kIdWinCameraPreview,
  kIdWinResetLayout,

  kIdCreateBase = wxID_HIGHEST + 300,
};

struct PresetMenuItem {
  int offset;
  const char* label;
  EditorScene::Preset preset;
  const char* submenu;
};

const PresetMenuItem kPresets[] = {
    {0, "Empty Node", EditorScene::Preset::kEmpty, nullptr},
    {1, "Cube", EditorScene::Preset::kCube, "3D Object"},
    {2, "Sphere", EditorScene::Preset::kSphere, "3D Object"},
    {3, "Capsule", EditorScene::Preset::kCapsule, "3D Object"},
    {4, "Cylinder", EditorScene::Preset::kCylinder, "3D Object"},
    {5, "Plane", EditorScene::Preset::kPlane, "3D Object"},
    {6, "Quad", EditorScene::Preset::kQuad, "3D Object"},
    {7, "Directional Light", EditorScene::Preset::kDirectionalLight, "Light"},
    {8, "Point Light", EditorScene::Preset::kPointLight, "Light"},
    {9, "Area Light", EditorScene::Preset::kAreaLight, "Light"},
    {10, "Light Probe Volume", EditorScene::Preset::kLightProbeVolume, "Light"},
    {11, "2D Sprite", EditorScene::Preset::kSprite, nullptr},
    {12, "Camera", EditorScene::Preset::kCamera, nullptr},
};

fs::path LayoutFilePath() {
  return EditorSettings::ConfigDir() / "editor_aui_layout.txt";
}

bool IsTextInputFocused() {
  wxWindow* focus = wxWindow::FindFocus();
  if (!focus) return false;
  return dynamic_cast<wxTextCtrl*>(focus) != nullptr;
}

}  // namespace

EditorFrame::EditorFrame(EditorScene& scene,
                         std::function<void()> on_close_project,
                         std::function<void()> on_exit_app)
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(scene.WindowTitle()),
              wxDefaultPosition, wxSize(1560, 920)),
      scene_(scene),
      on_close_project_(std::move(on_close_project)),
      on_exit_app_(std::move(on_exit_app)),
      aui_(this, wxAUI_MGR_DEFAULT | wxAUI_MGR_ALLOW_ACTIVE_PANE |
                     wxAUI_MGR_LIVE_RESIZE) {
  ApplyDarkPalette(this);
  aui_.SetArtProvider(new DarkDockArt());

  BuildMenuBar();
  BuildToolbar();
  BuildAuiPanes();
  SetupMenuBindings();

  // Dark status bar for live hints & path info
  CreateStatusBar(2);
  if (auto* sb = GetStatusBar()) {
    ApplyDarkPalette(sb);
    sb->SetStatusWidths(2, new int[2]{-1, 280});
  }

  Bind(wxEVT_CHAR_HOOK, &EditorFrame::OnCharHook, this);
  Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
  aui_.Bind(wxEVT_AUI_PANE_CLOSE, &EditorFrame::OnAuiPaneClose, this);

  SyncWindowMenuChecks();
  RefreshToolbarAndStatus(0.0f);
  CentreOnScreen();
}

EditorFrame::~EditorFrame() {
  DestroyChildFrames();
  aui_.UnInit();
}

void EditorFrame::DestroyChildFrames() {
  if (camera_preview_frame_) {
    camera_preview_frame_->Destroy();
    camera_preview_frame_ = nullptr;
  }
  if (ui_builder_frame_) {
    ui_builder_frame_->Destroy();
    ui_builder_frame_ = nullptr;
  }
  if (input_actions_frame_) {
    input_actions_frame_->Destroy();
    input_actions_frame_ = nullptr;
  }
}

void EditorFrame::BuildMenuBar() {
  auto* mb = new wxMenuBar();

  auto* file_menu = new wxMenu();
  file_menu->Append(kIdFileNewMap, "New Map...\tCtrl+N");
  file_menu->Append(kIdFileOpenMap, "Open Map...\tCtrl+O");
  file_menu->Append(kIdFileSave, "Save\tCtrl+S");
  file_menu->Append(kIdFileSaveAs, "Save Map As...\tCtrl+Shift+S");
  file_menu->Append(kIdFileSetMainMap, "Set Current Map as Main Map");
  file_menu->AppendSeparator();
  file_menu->Append(kIdFileBuildGame, "Build Game...\tCtrl+B");
  file_menu->Append(kIdFilePreferences, "Preferences...");
  file_menu->AppendSeparator();
  file_menu->Append(kIdFileCloseProject, "Close Project");
  file_menu->Append(kIdFileExit, "Exit\tCtrl+Q");
  mb->Append(file_menu, "&File");

  // Note: Omit accelerator tabs on Undo/Redo/Duplicate/Delete so wxTextCtrl
  // continues receiving Del/Backspace/Ctrl+Z natively; CHAR_HOOK handles them
  // cleanly whenever focus is outside text entry widgets.
  auto* edit_menu = new wxMenu();
  edit_menu->Append(kIdEditUndo, "Undo    (Ctrl+Z)");
  edit_menu->Append(kIdEditRedo, "Redo    (Ctrl+Shift+Z)");
  edit_menu->AppendSeparator();
  edit_menu->Append(kIdEditDuplicate, "Duplicate    (Ctrl+D)");
  edit_menu->Append(kIdEditDelete, "Delete    (Del)");
  mb->Append(edit_menu, "&Edit");

  auto* go_menu = new wxMenu();
  auto* sub_3d = new wxMenu();
  auto* sub_light = new wxMenu();
  bool inserted_3d = false;
  bool inserted_light = false;
  for (const auto& item : kPresets) {
    const int id = kIdCreateBase + item.offset;
    if (!item.submenu) {
      if (item.offset == 0) {
        go_menu->Append(id, item.label);
        go_menu->AppendSeparator();
      } else {
        go_menu->Append(id, item.label);
      }
    } else if (std::string(item.submenu) == "3D Object") {
      sub_3d->Append(id, item.label);
      if (!inserted_3d) {
        go_menu->AppendSubMenu(sub_3d, "3D Object");
        inserted_3d = true;
      }
    } else if (std::string(item.submenu) == "Light") {
      sub_light->Append(id, item.label);
      if (!inserted_light) {
        go_menu->AppendSubMenu(sub_light, "Light");
        inserted_light = true;
      }
    }
  }
  mb->Append(go_menu, "&GameObject");

  auto* proj_menu = new wxMenu();
  proj_menu->Append(kIdProjectBuildScripts, "Build Scripts\tF7");
  proj_menu->Append(kIdProjectTogglePlay, "Play / Stop\tF5");
  mb->Append(proj_menu, "&Project");

  window_menu_ = new wxMenu();
  mi_hierarchy_ = window_menu_->AppendCheckItem(kIdWinHierarchy, "Hierarchy");
  mi_scene_ = window_menu_->AppendCheckItem(kIdWinScene, "Scene Viewport");
  mi_inspector_ = window_menu_->AppendCheckItem(kIdWinInspector, "Inspector");
  mi_project_ =
      window_menu_->AppendCheckItem(kIdWinProject, "Project Browser");
  mi_console_ = window_menu_->AppendCheckItem(kIdWinConsole, "Console");
  mi_lore_ = window_menu_->AppendCheckItem(kIdWinLore, "Lore VCS");
  window_menu_->AppendSeparator();
  mi_uibuilder_ = window_menu_->AppendCheckItem(kIdWinUIBuilder, "UI Builder");
  mi_input_actions_ =
      window_menu_->AppendCheckItem(kIdWinInputActions, "Input Actions");
  window_menu_->Append(kIdWinPopoutUIBuilder,
                       "Pop Out UI Builder Window...");
  window_menu_->Append(kIdWinPopoutInputActions,
                       "Pop Out Input Actions Window...");
  mi_cam_preview_ = window_menu_->AppendCheckItem(
      kIdWinCameraPreview, "Camera Preview Floating Window");
  window_menu_->AppendSeparator();
  window_menu_->Append(kIdWinResetLayout, "Reset Window Layout");
  mb->Append(window_menu_, "&Window");

  SetMenuBar(mb);
}

void EditorFrame::BuildToolbar() {
  toolbar_ = new wxPanel(this);
  ApplyDarkPalette(toolbar_);
  toolbar_->SetBackgroundColour(Colors::kBgHeader);
  auto* s = new wxBoxSizer(wxHORIZONTAL);

  play_btn_ = new wxButton(toolbar_, wxID_ANY, "Play (F5)", wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(play_btn_, false, Colors::kAccentGreen);
  play_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { HandleTogglePlay(); });

  build_scripts_btn_ =
      new wxButton(toolbar_, wxID_ANY, "Build Scripts", wxDefaultPosition,
                   wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(build_scripts_btn_);
  build_scripts_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (auto p = scene_.GetProject()) scene_.PlayRef().Build(*p);
  });

  build_game_btn_ =
      new wxButton(toolbar_, wxID_ANY, "Build Game...", wxDefaultPosition,
                   wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(build_game_btn_);
  build_game_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    ShowReleaseBuildDialog(this, scene_);
  });

  save_btn_ = new wxButton(toolbar_, wxID_ANY, "Save", wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(save_btn_, true);
  save_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.SaveAllDirty();
    RefreshToolbarAndStatus(0.0f);
  });

  undo_btn_ = new wxButton(toolbar_, wxID_ANY, "Undo", wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
  redo_btn_ = new wxButton(toolbar_, wxID_ANY, "Redo", wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(undo_btn_);
  StyleButton(redo_btn_);
  undo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { scene_.Undo(); });
  redo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { scene_.Redo(); });

  map_status_lbl_ = new wxStaticText(toolbar_, wxID_ANY, "");
  map_status_lbl_->SetForegroundColour(Colors::kTextSecondary);

  fps_lbl_ = new wxStaticText(toolbar_, wxID_ANY, "0.0 fps");
  fps_lbl_->SetForegroundColour(Colors::kTextMuted);

  s->AddSpacer(6);
  s->Add(save_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  s->Add(undo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  s->Add(redo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
  s->Add(new wxStaticLine(toolbar_, wxID_ANY, wxDefaultPosition, wxSize(1, 20),
                          wxLI_VERTICAL),
         0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
  s->Add(play_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  s->Add(build_scripts_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  s->Add(build_game_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
  s->Add(map_status_lbl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  s->Add(fps_lbl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  toolbar_->SetSizer(s);
  toolbar_->SetMinSize(wxSize(-1, 36));
}

void EditorFrame::BuildAuiPanes() {
  hierarchy_panel_ = new HierarchyPanel(this, scene_);
  scene_panel_ = new SceneViewportPanel(this, scene_);
  inspector_panel_ = new InspectorPanel(this, scene_);
  project_panel_ = new ProjectBrowserPanel(this, scene_);
  console_panel_ = new ConsolePanel(this, scene_);
  lore_panel_ = new LorePanel(this, scene_.LoreRef());
  ui_builder_panel_ = new UIBuilderPanel(this, scene_);
  input_actions_panel_ = new InputActionsPanel(this, scene_);

  scene_panel_->SetOnPopoutPreview(
      [this](bool show) { ShowCameraPreviewPopout(show); });
  ui_builder_panel_->SetOnPopout(
      [this](bool show) { ShowUIBuilderPopout(show); });
  input_actions_panel_->SetOnPopout(
      [this](bool show) { ShowInputActionsPopout(show); });

  aui_.AddPane(toolbar_, wxAuiPaneInfo()
                             .Name("toolbar")
                             .ToolbarPane()
                             .Top()
                             .PaneBorder(false)
                             .Gripper(false)
                             .LeftDockable(false)
                             .RightDockable(false)
                             .BottomDockable(false)
                             .MinSize(wxSize(-1, 36))
                             .BestSize(wxSize(-1, 36)));

  aui_.AddPane(scene_panel_, wxAuiPaneInfo()
                                 .Name("scene")
                                 .Caption("Scene Viewport")
                                 .CenterPane());

  aui_.AddPane(hierarchy_panel_, wxAuiPaneInfo()
                                     .Name("hierarchy")
                                     .Caption("Hierarchy")
                                     .Left()
                                     .Layer(1)
                                     .BestSize(wxSize(270, 520))
                                     .MinSize(wxSize(190, 200))
                                     .CloseButton(true));

  aui_.AddPane(inspector_panel_, wxAuiPaneInfo()
                                     .Name("inspector")
                                     .Caption("Inspector")
                                     .Right()
                                     .Layer(1)
                                     .BestSize(wxSize(360, 520))
                                     .MinSize(wxSize(260, 200))
                                     .CloseButton(true));

  aui_.AddPane(project_panel_, wxAuiPaneInfo()
                                   .Name("project")
                                   .Caption("Project Browser")
                                   .Bottom()
                                   .Layer(0)
                                   .Position(0)
                                   .BestSize(wxSize(680, 250))
                                   .MinSize(wxSize(240, 150))
                                   .CloseButton(true));

  aui_.AddPane(console_panel_, wxAuiPaneInfo()
                                   .Name("console")
                                   .Caption("Console")
                                   .Bottom()
                                   .Layer(0)
                                   .Position(1)
                                   .BestSize(wxSize(460, 250))
                                   .MinSize(wxSize(220, 150))
                                   .CloseButton(true));

  aui_.AddPane(lore_panel_, wxAuiPaneInfo()
                                .Name("lore")
                                .Caption("Lore VCS")
                                .Bottom()
                                .Layer(0)
                                .Position(2)
                                .BestSize(wxSize(420, 250))
                                .MinSize(wxSize(220, 150))
                                .Show(scene_.Settings().show_lore)
                                .CloseButton(true));

  aui_.AddPane(ui_builder_panel_, wxAuiPaneInfo()
                                      .Name("ui_builder")
                                      .Caption("UI Builder")
                                      .Bottom()
                                      .Layer(2)
                                      .BestSize(wxSize(980, 380))
                                      .MinSize(wxSize(480, 280))
                                      .Show(scene_.Settings().show_ui_builder)
                                      .CloseButton(true));

  aui_.AddPane(
      input_actions_panel_,
      wxAuiPaneInfo()
          .Name("input_actions")
          .Caption("Input Actions")
          .Bottom()
          .Layer(2)
          .BestSize(wxSize(880, 360))
          .MinSize(wxSize(440, 260))
          .Show(scene_.Settings().show_input_actions)
          .CloseButton(true));

  aui_.Update();
  default_perspective_ = aui_.SavePerspective();

  // Load saved perspective if it exists
  std::ifstream in(LayoutFilePath());
  if (in.is_open()) {
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string saved = ss.str();
    if (!saved.empty()) {
      aui_.LoadPerspective(wxString::FromUTF8(saved), false);
      // Enforce persisted panel visibility flags
      if (auto& p = aui_.GetPane("lore"); p.IsOk()) {
        p.Show(scene_.Settings().show_lore);
      }
      if (auto& p = aui_.GetPane("ui_builder"); p.IsOk()) {
        p.Show(scene_.Settings().show_ui_builder);
      }
      if (auto& p = aui_.GetPane("input_actions"); p.IsOk()) {
        p.Show(scene_.Settings().show_input_actions);
      }
      aui_.Update();
    }
  }
}

void EditorFrame::SetupMenuBindings() {
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { HandleNewMap(); }, kIdFileNewMap);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { HandleOpenMap(); },
       kIdFileOpenMap);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { scene_.SaveAllDirty(); },
      kIdFileSave);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { HandleSaveMapAs(); },
       kIdFileSaveAs);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { scene_.SetMainMap(); },
      kIdFileSetMainMap);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent&) { ShowReleaseBuildDialog(this, scene_); },
      kIdFileBuildGame);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent&) { ShowPreferencesDialog(this, scene_); },
      kIdFilePreferences);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent&) {
        if (!ConfirmDiscardOrSave("Close this project?")) return;
        SaveLayoutPerspective();
        DestroyChildFrames();
        scene_.CloseProject();
        if (on_close_project_) {
          CallAfter([cb = on_close_project_]() { cb(); });
        }
      },
      kIdFileCloseProject);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(false); }, kIdFileExit);

  Bind(wxEVT_MENU, [this](wxCommandEvent&) { scene_.Undo(); }, kIdEditUndo);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { scene_.Redo(); }, kIdEditRedo);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { scene_.DuplicateSelected(); },
      kIdEditDuplicate);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { scene_.DeleteSelected(); },
      kIdEditDelete);

  for (const auto& item : kPresets) {
    Bind(
        wxEVT_MENU,
        [this, preset = item.preset](wxCommandEvent&) {
          MapNode* parent = scene_.GetSelectedNode();
          if (!parent) parent = scene_.GetMap().GetRoot();
          if (parent) scene_.CreatePreset(parent, preset);
        },
        kIdCreateBase + item.offset);
  }

  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent&) {
        if (auto p = scene_.GetProject()) scene_.PlayRef().Build(*p);
      },
      kIdProjectBuildScripts);
  Bind(wxEVT_MENU, [this](wxCommandEvent&) { HandleTogglePlay(); },
       kIdProjectTogglePlay);

  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) { TogglePane("hierarchy", evt.IsChecked()); },
      kIdWinHierarchy);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) { TogglePane("scene", evt.IsChecked()); },
      kIdWinScene);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) { TogglePane("inspector", evt.IsChecked()); },
      kIdWinInspector);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) { TogglePane("project", evt.IsChecked()); },
      kIdWinProject);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) { TogglePane("console", evt.IsChecked()); },
      kIdWinConsole);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) {
        scene_.Settings().show_lore = evt.IsChecked();
        scene_.Settings().Save();
        TogglePane("lore", evt.IsChecked());
      },
      kIdWinLore);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) {
        scene_.Settings().show_ui_builder = evt.IsChecked();
        scene_.Settings().Save();
        if (evt.IsChecked() && ui_builder_frame_) {
          ui_builder_frame_->Raise();
        } else {
          if (!evt.IsChecked() && ui_builder_frame_) {
            ShowUIBuilderPopout(false);
          }
          TogglePane("ui_builder", evt.IsChecked());
        }
      },
      kIdWinUIBuilder);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) {
        scene_.Settings().show_input_actions = evt.IsChecked();
        scene_.Settings().Save();
        if (evt.IsChecked() && input_actions_frame_) {
          input_actions_frame_->Raise();
        } else {
          if (!evt.IsChecked() && input_actions_frame_) {
            ShowInputActionsPopout(false);
          }
          TogglePane("input_actions", evt.IsChecked());
        }
      },
      kIdWinInputActions);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { ShowUIBuilderPopout(true); },
      kIdWinPopoutUIBuilder);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent&) { ShowInputActionsPopout(true); },
      kIdWinPopoutInputActions);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent& evt) {
        ShowCameraPreviewPopout(evt.IsChecked());
      },
      kIdWinCameraPreview);
  Bind(
      wxEVT_MENU,
      [this](wxCommandEvent&) {
        if (!default_perspective_.empty()) {
          aui_.LoadPerspective(default_perspective_, true);
          SyncWindowMenuChecks();
        }
      },
      kIdWinResetLayout);
}

void EditorFrame::TogglePane(const char* pane_name, bool show) {
  auto& pane = aui_.GetPane(pane_name);
  if (!pane.IsOk()) return;
  pane.Show(show);
  aui_.Update();
  SyncWindowMenuChecks();
}

void EditorFrame::ShowCameraPreviewPopout(bool show) {
  if (show) {
    if (!camera_preview_frame_) {
      camera_preview_frame_ = new CameraPreviewFrame(
          this, scene_, [this]() {
            camera_preview_frame_ = nullptr;
            scene_panel_->SetPreviewPoppedOut(false);
            SyncWindowMenuChecks();
          });
    }
    camera_preview_frame_->Show();
    camera_preview_frame_->Raise();
    scene_panel_->SetPreviewPoppedOut(true);
  } else if (camera_preview_frame_) {
    camera_preview_frame_->Close(true);
    camera_preview_frame_ = nullptr;
    scene_panel_->SetPreviewPoppedOut(false);
  }
  SyncWindowMenuChecks();
}

void EditorFrame::ShowUIBuilderPopout(bool show) {
  if (show) {
    scene_.Settings().show_ui_builder = true;
    scene_.Settings().Save();
    TogglePane("ui_builder", false);
    if (!ui_builder_frame_) {
      ui_builder_frame_ = new UIBuilderFrame(this, scene_, [this]() {
        ui_builder_frame_ = nullptr;
        SyncWindowMenuChecks();
      });
    }
    ui_builder_frame_->Show();
    ui_builder_frame_->Raise();
  } else {
    if (ui_builder_frame_) {
      ui_builder_frame_->Destroy();
      ui_builder_frame_ = nullptr;
    }
    TogglePane("ui_builder", scene_.Settings().show_ui_builder);
  }
  SyncWindowMenuChecks();
}

void EditorFrame::ShowInputActionsPopout(bool show) {
  if (show) {
    scene_.Settings().show_input_actions = true;
    scene_.Settings().Save();
    TogglePane("input_actions", false);
    if (!input_actions_frame_) {
      input_actions_frame_ = new InputActionsFrame(this, scene_, [this]() {
        input_actions_frame_ = nullptr;
        SyncWindowMenuChecks();
      });
    }
    input_actions_frame_->Show();
    input_actions_frame_->Raise();
  } else {
    if (input_actions_frame_) {
      input_actions_frame_->Destroy();
      input_actions_frame_ = nullptr;
    }
    TogglePane("input_actions", scene_.Settings().show_input_actions);
  }
  SyncWindowMenuChecks();
}

void EditorFrame::RevealUIBuilder() {
  scene_.Settings().show_ui_builder = true;
  if (ui_builder_frame_) {
    ui_builder_frame_->Show();
    ui_builder_frame_->Raise();
  } else {
    TogglePane("ui_builder", true);
  }
}

void EditorFrame::RevealInputActions() {
  scene_.Settings().show_input_actions = true;
  if (input_actions_frame_) {
    input_actions_frame_->Show();
    input_actions_frame_->Raise();
  } else {
    TogglePane("input_actions", true);
  }
}

void EditorFrame::SyncWindowMenuChecks() {
  if (!window_menu_) return;
  auto is_shown = [&](const char* name) {
    auto& p = aui_.GetPane(name);
    return p.IsOk() && p.IsShown();
  };
  mi_hierarchy_->Check(is_shown("hierarchy"));
  mi_scene_->Check(is_shown("scene"));
  mi_inspector_->Check(is_shown("inspector"));
  mi_project_->Check(is_shown("project"));
  mi_console_->Check(is_shown("console"));
  mi_lore_->Check(is_shown("lore"));
  mi_uibuilder_->Check(is_shown("ui_builder") || ui_builder_frame_ != nullptr);
  mi_input_actions_->Check(is_shown("input_actions") ||
                           input_actions_frame_ != nullptr);
  mi_cam_preview_->Check(camera_preview_frame_ != nullptr);
}

void EditorFrame::HandleTogglePlay() {
  auto proj = scene_.GetProject();
  if (!proj) return;
  auto& play = scene_.PlayRef();
  if (play.GetState() == PlaySession::State::kIdle) {
    if (scene_.IsDirty()) scene_.SaveMap();
    play.Play(*proj, scene_.GetMapPath());
  } else {
    play.Stop();
  }
}

void EditorFrame::HandleNewMap() {
  auto name = PromptTextDialog(this, "New Map",
                               "Map resource path (e.g. res://maps/level2.mmap):",
                               "res://maps/new_map.mmap");
  if (name.has_value() && !name->empty()) {
    scene_.NewMap(*name);
  }
}

void EditorFrame::HandleOpenMap() {
  auto proj = scene_.GetProject();
  if (!proj) return;
  if (!ConfirmDiscardOrSave("Open another map?")) return;

  std::string default_dir = proj->GetRoot().string();
  if (fs::exists(proj->GetRoot() / "maps")) {
    default_dir = (proj->GetRoot() / "maps").string();
  }
  wxFileDialog dlg(this, "Open Map", wxString::FromUTF8(default_dir), "",
                   "Mercury Map (*.mmap)|*.mmap",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() == wxID_OK) {
    std::string res = proj->ToResPath(fs::path(dlg.GetPath().ToStdString()));
    if (!res.empty()) scene_.OpenMap(res);
  }
}

void EditorFrame::HandleSaveMapAs() {
  auto proj = scene_.GetProject();
  if (!proj) return;
  std::string initial =
      scene_.GetMapPath().empty() ? "res://maps/main.mmap" : scene_.GetMapPath();
  auto res = PromptTextDialog(this, "Save Map As", "Resource path:",
                              wxString::FromUTF8(initial), "Save");
  if (res.has_value() && !res->empty()) {
    scene_.SaveNewMapAs(*res);
  }
}

bool EditorFrame::ConfirmDiscardOrSave(const wxString& action_prompt) {
  if (!scene_.HasUnsavedChanges()) return true;
  UnsavedChoice choice = ShowUnsavedChangesDialog(
      this, action_prompt + "\nYou have unsaved changes.");
  if (choice == UnsavedChoice::kCancel) return false;
  if (choice == UnsavedChoice::kSave) {
    if (!scene_.SaveAllDirty()) return false;
  } else {
    scene_.DiscardAllChanges();
  }
  return true;
}

void EditorFrame::SaveLayoutPerspective() {
  std::error_code ec;
  fs::create_directories(EditorSettings::ConfigDir(), ec);
  std::ofstream out(LayoutFilePath(), std::ios::trunc);
  if (out.is_open()) {
    out << aui_.SavePerspective().ToStdString();
  }
}

void EditorFrame::OnAuiPaneClose(wxAuiManagerEvent& evt) {
  wxAuiPaneInfo* pane = evt.GetPane();
  if (!pane) return;
  const wxString& name = pane->name;
  if (name == "lore") {
    scene_.Settings().show_lore = false;
    scene_.Settings().Save();
  } else if (name == "ui_builder") {
    scene_.Settings().show_ui_builder = false;
    scene_.Settings().Save();
  } else if (name == "input_actions") {
    scene_.Settings().show_input_actions = false;
    scene_.Settings().Save();
  }
  CallAfter([this]() { SyncWindowMenuChecks(); });
}

void EditorFrame::OnCharHook(wxKeyEvent& evt) {
  if (IsTextInputFocused()) {
    evt.Skip();
    return;
  }
  const int kc = evt.GetKeyCode();
  const bool ctrl = evt.ControlDown();
  const bool shift = evt.ShiftDown();

  if (ctrl && !shift && (kc == 'Z' || kc == 'z')) {
    scene_.Undo();
    return;
  }
  if ((ctrl && shift && (kc == 'Z' || kc == 'z')) ||
      (ctrl && !shift && (kc == 'Y' || kc == 'y'))) {
    scene_.Redo();
    return;
  }
  if (ctrl && !shift && (kc == 'D' || kc == 'd')) {
    scene_.DuplicateSelected();
    return;
  }
  if (kc == WXK_DELETE) {
    scene_.DeleteSelected();
    return;
  }
  if (!ctrl && (kc == 'F' || kc == 'f')) {
    scene_.FrameSelected();
    return;
  }
  if (!ctrl && (kc == 'G' || kc == 'g')) {
    scene_.ToggleHandleSpace();
    return;
  }
  evt.Skip();
}

void EditorFrame::OnClose(wxCloseEvent& evt) {
  if (closing_) {
    evt.Skip();
    return;
  }
  if (evt.CanVeto() && !ConfirmDiscardOrSave("Exit Mercury Editor?")) {
    evt.Veto();
    return;
  }
  closing_ = true;
  SaveLayoutPerspective();
  scene_.Settings().Save();
  DestroyChildFrames();
  if (on_exit_app_) on_exit_app_();
  evt.Skip();
}

void EditorFrame::RefreshToolbarAndStatus(float dt) {
  auto& play = scene_.PlayRef();
  const auto pstate = play.GetState();
  if (pstate == PlaySession::State::kIdle) {
    play_btn_->SetLabel("Play (F5)");
    play_btn_->SetBackgroundColour(Colors::kAccentGreen);
    play_btn_->Enable(scene_.GetProject() != nullptr);
  } else if (pstate == PlaySession::State::kBuilding) {
    play_btn_->SetLabel("Building...");
    play_btn_->SetBackgroundColour(Colors::kAccentAmber);
    play_btn_->Enable(false);
  } else {
    play_btn_->SetLabel("Stop (F5)");
    play_btn_->SetBackgroundColour(Colors::kAccentRed);
    play_btn_->Enable(true);
  }

  const bool busy = play.IsBusy() || scene_.ReleaseRef().IsBusy();
  build_scripts_btn_->Enable(!busy && scene_.GetProject() != nullptr);
  build_game_btn_->Enable(!busy && scene_.GetProject() != nullptr);

  save_btn_->Enable(scene_.HasUnsavedChanges());
  undo_btn_->Enable(scene_.CanUndo());
  redo_btn_->Enable(scene_.CanRedo());

  std::string map_lbl = scene_.MapDisplayName();
  if (scene_.IsDirty()) map_lbl += " *";
  map_status_lbl_->SetLabel(wxString::FromUTF8(map_lbl));

  fps_accum_ += dt;
  fps_frames_++;
  if (fps_accum_ >= 0.5f) {
    fps_display_ = static_cast<float>(fps_frames_) / fps_accum_;
    fps_accum_ = 0.0f;
    fps_frames_ = 0;
    fps_lbl_->SetLabel(wxString::Format("%.1f fps", fps_display_));
  }

  if (auto* sb = GetStatusBar()) {
    sb->SetStatusText(wxString::FromUTF8(scene_.GetHintText()), 0);
    std::string proj_name =
        scene_.GetProject() ? scene_.GetProject()->GetName() : "No Project";
    sb->SetStatusText(wxString::FromUTF8(proj_name), 1);
  }
}

void EditorFrame::SyncFromController(float dt) {
  if (closing_) return;

  if (scene_.ConsumeUIBuilderFocusRequest()) RevealUIBuilder();
  if (scene_.ConsumeInputActionsFocusRequest()) RevealInputActions();

  RefreshToolbarAndStatus(dt);

  if (hierarchy_panel_ && hierarchy_panel_->IsShownOnScreen()) {
    hierarchy_panel_->SyncFromController();
  }
  if (scene_panel_ && scene_panel_->IsShownOnScreen()) {
    scene_panel_->SyncFromController(dt);
  }
  if (inspector_panel_ && inspector_panel_->IsShownOnScreen()) {
    inspector_panel_->SyncFromController();
  }
  if (project_panel_ && project_panel_->IsShownOnScreen()) {
    project_panel_->SyncFromController();
  }
  if (console_panel_ && console_panel_->IsShownOnScreen()) {
    console_panel_->SyncFromController();
  }
  if (lore_panel_ && lore_panel_->IsShownOnScreen()) {
    lore_panel_->SyncFromController();
  }
  if (ui_builder_panel_ && ui_builder_panel_->IsShownOnScreen()) {
    ui_builder_panel_->SyncFromController(dt);
  }
  if (input_actions_panel_ && input_actions_panel_->IsShownOnScreen()) {
    input_actions_panel_->SyncFromController(dt);
  }

  if (camera_preview_frame_) camera_preview_frame_->SyncFromController();
  if (ui_builder_frame_) ui_builder_frame_->SyncFromController(dt);
  if (input_actions_frame_) input_actions_frame_->SyncFromController(dt);
}

}  // namespace Mercury::EditorUI
