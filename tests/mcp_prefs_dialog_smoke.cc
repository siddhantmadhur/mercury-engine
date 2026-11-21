// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Mercury Preferences dialog (MCP tab) smoke test.
//
// The dialog is modal, so this is a standalone wx executable: it opens the
// real dialog and a CallAfter flips to the MCP tab, exercises the controls
// and closes it. It covers tab switching, that Cancel drops the persisted
// settings (immediate host actions are not undoable by Cancel), that Save
// persists the MCP settings and applies them to the MCP host synchronously,
// that the first enable generates a token, and that the copy-ready
// registration command is filled in.

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "dialogs.h"
#include "editor_scene.h"
#include "project/project.h"
#include "templates.h"

namespace {

using namespace Mercury;
using namespace Mercury::EditorUI;

namespace fs = std::filesystem;

template <typename T>
T* FindNamed(wxWindow* root, const char* name) {
  if (auto* typed = dynamic_cast<T*>(root); typed && root->GetName() == name) {
    return typed;
  }
  for (wxWindow* child : root->GetChildren()) {
    if (T* found = FindNamed<T>(child, name)) return found;
  }
  return nullptr;
}

wxDialog* FindPrefsDialog() {
  for (wxWindow* window : wxTopLevelWindows) {
    auto* dialog = dynamic_cast<wxDialog*>(window);
    if (dialog && dialog->GetName() == "preferences_dialog") return dialog;
  }
  return nullptr;
}

class McpPrefsSmokeApp : public wxApp {
 public:
  bool OnInit() override {
    wxInitAllImageHandlers();

    config_dir_ =
        fs::temp_directory_path() /
        ("mercury_mcp_prefs_smoke_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(config_dir_);
    previous_xdg_ = std::getenv("XDG_CONFIG_HOME")
                        ? std::getenv("XDG_CONFIG_HOME")
                        : std::string();
    had_xdg_ = std::getenv("XDG_CONFIG_HOME") != nullptr;
    setenv("XDG_CONFIG_HOME", config_dir_.c_str(), 1);

    const fs::path parent = config_dir_ / "project";
    std::string error;
    const fs::path mproj =
        Templates::CreateProject(parent, "MCP Prefs Smoke", false, &error);
    if (mproj.empty()) {
      wxPrintf("could not create the project: %s\n", error);
      return false;
    }

    frame_ = new wxFrame(nullptr, wxID_ANY, "MCP preferences smoke");
    frame_->Show(true);
    SetTopWindow(frame_);

    scene_ = std::make_unique<EditorScene>();
    if (!scene_->OpenProject(mproj)) {
      wxPrintf("could not open the project\n");
      return false;
    }

    CallAfter([this]() {
      Run();
      ExitMainLoop();
    });
    return true;
  }

  int OnExit() override {
    scene_.reset();
    frame_ = nullptr;
    if (had_xdg_) {
      setenv("XDG_CONFIG_HOME", previous_xdg_.c_str(), 1);
    } else {
      unsetenv("XDG_CONFIG_HOME");
    }
    std::error_code ec;
    fs::remove_all(config_dir_, ec);
    return exit_code_;
  }

 private:
  enum class Mode { kCancel, kSave };

  // Opens the dialog; the pending mode drives FillAndClose.
  void OpenDialog(Mode mode) {
    pending_mode_ = mode;
    CallAfter([this]() { FillAndClose(); });
    ShowPreferencesDialog(frame_, *scene_);
  }

  void Run() {
    // Cancel drops the flipped settings, but it is not absolute: immediate
    // host actions (Regenerate Token, the first-enable token generation)
    // survive it. This pass only checks the persisted settings fields, and
    // that Regenerate Token stays disabled while no token exists yet.
    OpenDialog(Mode::kCancel);
    const bool cancel_is_clean = !scene_->Settings().mcp_enabled &&
                                 scene_->Settings().mcp_port == 49152 &&
                                 !scene_->Settings().mcp_ephemeral &&
                                 !scene_->Settings().mcp_allow_edits;
    if (!cancel_is_clean) {
      wxPrintf("FAIL: Cancel applied MCP settings\n");
      exit_code_ = 1;
    }
    if (last_regen_enabled_) {
      wxPrintf("FAIL: Regenerate Token was enabled without a token\n");
      exit_code_ = 1;
    }

    // Save: the General tab stays default and the MCP tab persists. The
    // synchronous ApplySettings starts the server and generates the token
    // on first enable.
    OpenDialog(Mode::kSave);
    const EditorSettings& settings = scene_->Settings();
    if (!settings.mcp_enabled || settings.mcp_port != 50153 ||
        !settings.mcp_ephemeral || !settings.mcp_allow_edits ||
        settings.mcp_write_config) {
      wxPrintf("FAIL: Save did not persist the MCP settings\n");
      exit_code_ = 1;
    }
    if (scene_->Mcp().Url().find("127.0.0.1") == std::string::npos) {
      wxPrintf("FAIL: the MCP host URL is missing\n");
      exit_code_ = 1;
    }
    if (settings.mcp_token.empty()) {
      wxPrintf("FAIL: the first enable did not generate a token\n");
      exit_code_ = 1;
    }

    // Reopen: the token survived, so Regenerate Token is now available.
    OpenDialog(Mode::kCancel);
    if (scene_->Settings().mcp_token.empty() || !last_regen_enabled_) {
      wxPrintf("FAIL: Regenerate Token is not enabled with a token\n");
      exit_code_ = 1;
    }
    wxPrintf("mcp prefs dialog: %s\n", exit_code_ == 0 ? "ok" : "failed");
  }

  void FillAndClose() {
    wxDialog* dialog = FindPrefsDialog();
    if (!dialog) {
      wxPrintf("FAIL: the dialog did not appear\n");
      exit_code_ = 1;
      return;
    }
    auto* notebook = FindNamed<wxNotebook>(dialog, "prefs_notebook");
    if (!notebook) {
      wxPrintf("FAIL: the preferences notebook is missing\n");
      exit_code_ = 1;
      return;
    }
    if (notebook->GetPageCount() != 2) {
      wxPrintf("FAIL: expected General and MCP tabs\n");
      exit_code_ = 1;
    }
    notebook->SetSelection(1);
    if (notebook->GetSelection() != 1) {
      wxPrintf("FAIL: could not switch to the MCP tab\n");
      exit_code_ = 1;
    }

    if (!FindNamed<wxCheckBox>(dialog, "mcp_enable") ||
        !FindNamed<wxSpinCtrl>(dialog, "mcp_port") ||
        !FindNamed<wxCheckBox>(dialog, "mcp_ephemeral") ||
        !FindNamed<wxCheckBox>(dialog, "mcp_write_config") ||
        !FindNamed<wxTextCtrl>(dialog, "mcp_url") ||
        !FindNamed<wxTextCtrl>(dialog, "mcp_command")) {
      wxPrintf("FAIL: the MCP tab controls are missing\n");
      exit_code_ = 1;
      dialog->EndModal(wxID_CANCEL);
      return;
    }
    auto* command = FindNamed<wxTextCtrl>(dialog, "mcp_command");
    if (command->GetValue().find("claude mcp add") == std::string::npos ||
        command->GetValue().find("mercury") == std::string::npos) {
      wxPrintf("FAIL: the registration command is not filled in\n");
      exit_code_ = 1;
    }

    if (auto* enable = FindNamed<wxCheckBox>(dialog, "mcp_enable")) {
      enable->SetValue(true);
    }
    if (auto* port = FindNamed<wxSpinCtrl>(dialog, "mcp_port")) {
      port->SetValue(50153);
    }
    if (auto* ephemeral = FindNamed<wxCheckBox>(dialog, "mcp_ephemeral")) {
      ephemeral->SetValue(true);
    }
    if (auto* access = FindNamed<wxChoice>(dialog, "mcp_access")) {
      access->SetSelection(1);
    }
    if (auto* write_config =
            FindNamed<wxCheckBox>(dialog, "mcp_write_config")) {
      write_config->SetValue(false);
    }

    if (auto* regen = FindNamed<wxButton>(dialog, "mcp_regen")) {
      last_regen_enabled_ = regen->IsEnabled();
    }

    dialog->EndModal(pending_mode_ == Mode::kSave ? wxID_OK : wxID_CANCEL);
  }

  Mode pending_mode_ = Mode::kCancel;
  bool last_regen_enabled_ = false;
  std::unique_ptr<EditorScene> scene_;
  wxFrame* frame_ = nullptr;
  fs::path config_dir_;
  std::string previous_xdg_;
  bool had_xdg_ = false;
  int exit_code_ = 0;
};

}  // namespace

wxIMPLEMENT_APP(McpPrefsSmokeApp);
