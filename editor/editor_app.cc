#include "editor_app.h"

#include <wx/image.h>

#include <filesystem>
#include <string>

#include "os/filesystem.h"

namespace Mercury::EditorUI {

namespace {
// ~60 fps editor UI/simulation. The scene canvas gets its own higher-rate
// timer while the user is interacting with it.
constexpr int kFrameIntervalMs = 16;
constexpr int kViewportIntervalMs = 4;
}  // namespace

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
  frame_timer_.SetOwner(this);
  Bind(wxEVT_TIMER, &MercuryEditorApp::OnTimer, this, frame_timer_.GetId());
  viewport_timer_.SetOwner(this);
  Bind(wxEVT_TIMER, &MercuryEditorApp::OnViewportTimer, this,
       viewport_timer_.GetId());
  frame_timer_.Start(kFrameIntervalMs);
  return true;
}

int MercuryEditorApp::OnExit() {
  shutting_down_ = true;
  frame_timer_.Stop();
  viewport_timer_.Stop();
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
        frame_timer_.Stop();
        viewport_timer_.Stop();
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

void MercuryEditorApp::OnTimer(wxTimerEvent& /*evt*/) {
  if (shutting_down_ || in_tick_ || !application_ || !scene_) return;
  wxWindow* active_win = editor_frame_
                             ? static_cast<wxWindow*>(editor_frame_)
                             : static_cast<wxWindow*>(launcher_frame_);
  if (!active_win || !active_win->IsShown()) return;
  if (auto* top = dynamic_cast<wxTopLevelWindow*>(active_win);
      top && top->IsIconized()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  float dt = std::chrono::duration<float>(now - last_tick_).count();
  last_tick_ = now;
  if (dt < 0.0f || dt > 0.25f) dt = 0.016f;

  const bool want_viewport =
      editor_frame_ && editor_frame_->WantsUnlockedViewportFps();
  if (!want_viewport) viewport_timer_.Stop();

  in_tick_ = true;
  if (want_viewport) {
    // The viewport timer owns StepFrame at an unlocked rate while the user
    // navigates or drags the scene. Keep the rest of the editor at 60 fps.
    if (!viewport_timer_.IsRunning()) {
      last_viewport_tick_ = now;
      viewport_timer_.Start(kViewportIntervalMs);
    }
    editor_frame_->SyncPanels(dt);
    in_tick_ = false;
    return;
  }

  application_->StepFrame();

  if (editor_frame_) {
    editor_frame_->SyncFromController(dt);
  }

  in_tick_ = false;
}

void MercuryEditorApp::OnViewportTimer(wxTimerEvent& /*evt*/) {
  if (shutting_down_ || in_tick_ || !application_ || !editor_frame_) return;
  if (!editor_frame_->IsShown() || editor_frame_->IsIconized() ||
      !editor_frame_->WantsUnlockedViewportFps()) {
    viewport_timer_.Stop();
    return;
  }

  in_tick_ = true;
  const auto now = std::chrono::steady_clock::now();
  float dt = std::chrono::duration<float>(now - last_viewport_tick_).count();
  last_viewport_tick_ = now;
  if (dt <= 0.0f || dt > 0.25f) dt = 0.016f;

  application_->StepFrame();
  editor_frame_->SyncViewports(dt);

  in_tick_ = false;
}

}  // namespace Mercury::EditorUI
