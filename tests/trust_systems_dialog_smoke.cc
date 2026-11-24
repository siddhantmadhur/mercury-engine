// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Mercury trust dialog smoke test.
//
// ShowTrustSystemsDialog is modal, so this is a standalone wx executable. It
// opens the real dialog and a CallAfter clicks one of its buttons. A watchdog
// timer closes the dialog if the click did not, so a button whose click does
// nothing fails the test instead of hanging it.
//
// This is a regression test for wxID_YES/wxID_NO: wxDialog only closes itself
// for the affirmative id (wxID_OK by default) and the escape id (wxID_CANCEL),
// so the "Trust" and "Don't Trust" buttons need an explicit EndModal.

#include <wx/timer.h>
#include <wx/wx.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

#include "dialogs.h"

namespace {

using namespace Mercury;
using namespace Mercury::EditorUI;

namespace fs = std::filesystem;

constexpr int kWatchdogMs = 1000;
constexpr int kTimedOut = -999;
// The title of the dialog when the caller does not ask the first-time
// question. Matching it as well as the name keeps the test independent of the
// name the dialog gains with the fix.
const char* const kDialogTitle = "Editor System Trust";

const char* Describe(const std::optional<bool>& value) {
  if (!value) return "cancel";
  return *value ? "trusted" : "not trusted";
}

class TrustDialogSmokeApp : public wxApp {
 public:
  bool OnInit() override {
    wxInitAllImageHandlers();
    frame_ = new wxFrame(nullptr, wxID_ANY, "Trust dialog smoke");
    frame_->Show(true);
    SetTopWindow(frame_);
    CallAfter([this]() {
      Run();
      ExitMainLoop();
    });
    return true;
  }

  int OnRun() override {
    wxApp::OnRun();
    return exit_code_;
  }

  int OnExit() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
    return exit_code_;
  }

 private:
  void Run() {
    std::error_code ec;
    root_ = fs::temp_directory_path() / "mercury-trust-dialog-smoke";
    fs::create_directories(root_, ec);

    const bool trust_ok = RunCase("Trust", wxID_YES, true);
    const bool distrust_ok = RunCase("Don't Trust", wxID_NO, false);
    const bool cancel_ok = RunCase("Cancel", wxID_CANCEL, std::nullopt);
    const bool ok = trust_ok && distrust_ok && cancel_ok;
    wxPrintf("trust systems dialog: %s\n", ok ? "ok" : "failed");
    exit_code_ = ok ? 0 : 1;
  }

  // The dialog is on the stack while it is modal, but wxWidgets still lists it
  // with the top-level windows, so it can be found and driven from a CallAfter.
  wxDialog* FindDialog() const {
    for (wxWindow* window : wxTopLevelWindows) {
      auto* dialog = dynamic_cast<wxDialog*>(window);
      if (!dialog || !dialog->IsModal()) continue;
      if (dialog->GetName() == "trust_systems_dialog" ||
          dialog->GetTitle() == kDialogTitle) {
        return dialog;
      }
    }
    return nullptr;
  }

  // Ends whatever modal dialog is still open, so a click that does nothing
  // fails the test instead of hanging it.
  static void EndOpenDialog(int code) {
    for (wxWindow* window : wxTopLevelWindows) {
      auto* dialog = dynamic_cast<wxDialog*>(window);
      if (dialog && dialog->IsModal()) dialog->EndModal(code);
    }
  }

  void ClickButton() {
    clicked_ = false;
    wxDialog* dialog = FindDialog();
    if (!dialog) {
      wxPrintf("FAIL: the trust dialog did not appear\n");
      return;
    }
    wxWindow* button = dialog->FindWindow(pending_button_);
    if (!button) {
      wxPrintf("FAIL: the trust dialog has no button %d\n", pending_button_);
      dialog->EndModal(kTimedOut);
      return;
    }
    clicked_ = true;
    wxCommandEvent event(wxEVT_BUTTON, pending_button_);
    event.SetEventObject(button);
    button->GetEventHandler()->ProcessEvent(event);
  }

  bool RunCase(const char* label, int button_id, std::optional<bool> expected) {
    pending_button_ = button_id;
    clicked_ = false;

    wxTimer watchdog;
    watchdog.Bind(wxEVT_TIMER, [](wxTimerEvent&) { EndOpenDialog(kTimedOut); });
    CallAfter([this]() { ClickButton(); });
    watchdog.StartOnce(kWatchdogMs);

    const std::optional<bool> result = ShowTrustSystemsDialog(
        frame_, root_, "smoke_systems", root_ / "system", false, false);
    watchdog.Stop();

    if (!clicked_) {
      wxPrintf("FAIL: %s was not clicked\n", label);
      return false;
    }
    if (result != expected) {
      wxPrintf("FAIL: %s returned %s, expected %s\n", label, Describe(result),
               Describe(expected));
      return false;
    }
    wxPrintf("ok: %s returned %s\n", label, Describe(result));
    return true;
  }

  wxFrame* frame_ = nullptr;
  fs::path root_;
  int pending_button_ = wxID_ANY;
  bool clicked_ = false;
  int exit_code_ = 0;
};

}  // namespace

wxIMPLEMENT_APP(TrustDialogSmokeApp);
