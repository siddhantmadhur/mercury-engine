// mercury_editor_smoke <project.mproj> [frames]
//
// Runs the hosted headless engine + EditorScene + (when DISPLAY is present)
// the full wxWidgets EditorFrame for a bounded number of frames to smoke-test
// scene loading, Vulkan offscreen readback, and UI panel synchronization.

#include <wx/wx.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "application/default_application.h"
#include "debugger/debugger.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "os/filesystem.h"

namespace {

int RunHeadlessControllerSmoke(const std::filesystem::path& project,
                               int max_frames) {
  auto application = std::make_unique<Mercury::DefaultApplication>(
      "Mercury Editor (smoke)", 1280, 720);
  application->EnableHeadlessMode(true);
  auto scene = std::make_unique<Mercury::EditorScene>(project);
  Mercury::EditorScene* scene_ptr = scene.get();
  application->SetGameScene(std::move(scene));
  if (!application->StartHosted()) return 1;

  for (int i = 0; i < max_frames; ++i) {
    application->StepFrame();
    if (i == 2 && scene_ptr->GetMap().GetRoot()) {
      auto* created = scene_ptr->CreatePreset(
          scene_ptr->GetMap().GetRoot(), Mercury::EditorScene::Preset::kCube);
      if (created) {
        scene_ptr->Undo();
        scene_ptr->Redo();
        scene_ptr->Undo();
      }
    }
  }
  application.reset();
  return 0;
}

class SmokeWxApp : public wxApp {
 public:
  SmokeWxApp(std::filesystem::path project, int max_frames)
      : project_(std::move(project)), max_frames_(max_frames) {}

  bool OnInit() override {
    wxInitAllImageHandlers();
    application_ = std::make_unique<Mercury::DefaultApplication>(
        "Mercury Editor (wx smoke)", 1280, 720);
    application_->EnableHeadlessMode(true);
    auto scene = std::make_unique<Mercury::EditorScene>(project_);
    scene_ = scene.get();
    application_->SetGameScene(std::move(scene));
    if (!application_->StartHosted()) return false;

    frame_ = new Mercury::EditorUI::EditorFrame(
        *scene_, []() {}, [this]() { done_ = true; });
    frame_->Show(true);
    SetTopWindow(frame_);

    Bind(wxEVT_IDLE, &SmokeWxApp::OnIdle, this);
    return true;
  }

  int OnExit() override {
    done_ = true;
    frame_ = nullptr;
    scene_ = nullptr;
    application_.reset();
    return wxApp::OnExit();
  }

 private:
  void OnIdle(wxIdleEvent& evt) {
    if (done_ || !application_ || !frame_) return;
    application_->StepFrame();
    frame_->SyncFromController(0.016f);
    if (++frames_ >= max_frames_) {
      done_ = true;
      frame_->DestroyChildFrames();
      frame_->Destroy();
      frame_ = nullptr;
      ExitMainLoop();
      return;
    }
    evt.RequestMore(true);
  }

  std::filesystem::path project_;
  int max_frames_ = 10;
  int frames_ = 0;
  bool done_ = false;
  std::unique_ptr<Mercury::DefaultApplication> application_;
  Mercury::EditorScene* scene_ = nullptr;
  Mercury::EditorUI::EditorFrame* frame_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PDebug::error("usage: {} <project.mproj> [frames]", argv[0]);
    return 2;
  }
  const std::filesystem::path project = Mercury::os::UserPath(argv[1]);
  const int frames = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 10;

  const char* disp = std::getenv("DISPLAY");
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if ((!disp || !*disp) && (!wayland || !*wayland)) {
    return RunHeadlessControllerSmoke(project, frames);
  }

  wxApp::SetInstance(new SmokeWxApp(project, frames));
  return wxEntry(argc, argv);
}
