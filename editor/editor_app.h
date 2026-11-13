/*
 * editor_app.h
 *
 * wxApp entrypoint hosting headless DefaultApplication + EditorScene, driving
 * StepFrame() via a ~16ms wxTimer, and managing LauncherFrame <-> EditorFrame
 * transitions.
 */
#ifndef MERCURY_EDITOR_EDITOR_APP_H
#define MERCURY_EDITOR_EDITOR_APP_H

#include <wx/timer.h>
#include <wx/wx.h>

#include <chrono>
#include <memory>

#include "application/default_application.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "launcher_frame.h"

namespace Mercury::EditorUI {

class MercuryEditorApp : public wxApp {
 public:
  bool OnInit() override;
  int OnExit() override;
  int FilterEvent(wxEvent& evt) override;

 private:
  void OnTimer(wxTimerEvent& evt);
  void ShowLauncherFrame();
  void ShowEditorFrame();

  std::unique_ptr<DefaultApplication> application_;
  EditorScene* scene_ = nullptr;

  LauncherFrame* launcher_frame_ = nullptr;
  EditorFrame* editor_frame_ = nullptr;

  wxTimer timer_;
  std::chrono::steady_clock::time_point last_tick_;
  bool in_tick_ = false;
  bool shutting_down_ = false;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_EDITOR_APP_H
