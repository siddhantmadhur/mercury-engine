#include "editor_app.h"

#include <wx/image.h>

#include <filesystem>
#include <string>

#include "os/filesystem.h"

namespace Mercury::EditorUI {

bool MercuryEditorApp::OnInit() {
  wxInitAllImageHandlers();
  SetAppName("Mercury Editor");
  SetAppDisplayName("Mercury Editor");

  std::filesystem::path initial_project;
  std::string initial_map;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = wxString(argv[i]).ToStdString();
    if (arg == "--map" && i + 1 < argc) {
      initial_map = wxString(argv[++i]).ToStdString();
    } else if (!arg.starts_with("--")) {
      initial_project = Mercury::os::UserPath(arg);
    }
  }

  application_ =
      std::make_unique<DefaultApplication>("Mercury Editor", 1600, 900);
  application_->EnableHeadlessMode(true);

  auto scene = std::make_unique<EditorScene>(initial_project, initial_map);
  scene_ = scene.get();
  application_->SetGameScene(std::move(scene));

  application_->SetTitleCallback([this](const std::string& title) {
    if (editor_frame_ && !shutting_down_) {
      editor_frame_->SetTitle(wxString::FromUTF8(title));
    }
  });

  if (!application_->StartHosted()) {
    return false;
  }

  if (scene_->GetProject() != nullptr) {
    ShowEditorFrame();
  } else {
    ShowLauncherFrame();
  }

  last_tick_ = std::chrono::steady_clock::now();
  Bind(wxEVT_IDLE, &MercuryEditorApp::OnIdle, this);
  return true;
}

int MercuryEditorApp::OnExit() {
  shutting_down_ = true;
  launcher_frame_ = nullptr;
  editor_frame_ = nullptr;
  scene_ = nullptr;
  application_.reset();
  return wxApp::OnExit();
}

int MercuryEditorApp::FilterEvent(wxEvent& evt) {
  if (evt.GetEventType() == wxEVT_KEY_UP && editor_frame_ && !shutting_down_) {
    editor_frame_->NotifyGlobalKeyUp(static_cast<wxKeyEvent&>(evt));
  }
  return Event_Skip;
}

void MercuryEditorApp::ShowLauncherFrame() {
  auto* old_editor = editor_frame_;
  editor_frame_ = nullptr;

  launcher_frame_ = new LauncherFrame(*scene_, [this]() {
    if (scene_ && scene_->GetProject() != nullptr) {
      ShowEditorFrame();
    }
  });
  launcher_frame_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) {
    if (launcher_frame_) launcher_frame_ = nullptr;
    evt.Skip();
  });
  launcher_frame_->Show(true);
  SetTopWindow(launcher_frame_);

  if (old_editor) {
    old_editor->DestroyChildFrames();
    old_editor->Destroy();
  }
}

void MercuryEditorApp::ShowEditorFrame() {
  auto* old_launcher = launcher_frame_;
  launcher_frame_ = nullptr;

  editor_frame_ = new EditorFrame(
      *scene_,
      [this]() {
        if (!shutting_down_) ShowLauncherFrame();
      },
      [this]() {
        shutting_down_ = true;
      });
  editor_frame_->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& evt) {
    if (evt.GetEventObject() == editor_frame_) editor_frame_ = nullptr;
    evt.Skip();
  });
  editor_frame_->Show(true);
  SetTopWindow(editor_frame_);

  if (old_launcher) {
    old_launcher->Destroy();
  }
}

void MercuryEditorApp::OnIdle(wxIdleEvent& evt) {
  if (shutting_down_ || in_tick_ || !application_ || !scene_) return;
  wxWindow* active_win = editor_frame_
                             ? static_cast<wxWindow*>(editor_frame_)
                             : static_cast<wxWindow*>(launcher_frame_);
  if (!active_win || !active_win->IsShown()) return;
  if (auto* top = dynamic_cast<wxTopLevelWindow*>(active_win);
      top && top->IsIconized()) {
    return;
  }
  in_tick_ = true;

  const auto now = std::chrono::steady_clock::now();
  float dt = std::chrono::duration<float>(now - last_tick_).count();
  last_tick_ = now;
  if (dt < 0.0f || dt > 0.25f) dt = 0.016f;

  application_->StepFrame();

  if (editor_frame_) {
    editor_frame_->SyncFromController(dt);
  }

  in_tick_ = false;
  if (!shutting_down_) evt.RequestMore(true);
}

}  // namespace Mercury::EditorUI
