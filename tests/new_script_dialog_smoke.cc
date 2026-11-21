// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Mercury New Script dialog smoke test.
//
// The dialog is modal, so this is a standalone wx executable: it opens the
// real dialog and a CallAfter fills it in and closes it, then creates the
// script through EditorScene. It covers the four kinds, the "open after
// creating" preference, and the path preview indirectly (the preview label is
// not inspected, but the files it names are created).

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
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

wxDialog* FindNewScriptDialog() {
  for (wxWindow* window : wxTopLevelWindows) {
    auto* dialog = dynamic_cast<wxDialog*>(window);
    if (dialog && dialog->GetName() == "new_script_dialog") return dialog;
  }
  return nullptr;
}

class NewScriptSmokeApp : public wxApp {
 public:
  bool OnInit() override {
    wxInitAllImageHandlers();

    config_dir_ =
        fs::temp_directory_path() /
        ("mercury_new_script_smoke_" +
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
        Templates::CreateProject(parent, "Dialog Smoke", false, &error);
    if (mproj.empty()) {
      wxPrintf("could not create the project: %s\n", error);
      return false;
    }

    frame_ = new wxFrame(nullptr, wxID_ANY, "New Script dialog smoke");
    frame_->Show(true);
    SetTopWindow(frame_);

    scene_ = std::make_unique<EditorScene>();
    if (!scene_->OpenProject(mproj)) {
      wxPrintf("could not open the project\n");
      return false;
    }
    scene_->Settings().open_script_after_create = false;
    scene_->Settings().new_script_kind = Templates::ScriptKind::kAngelScript;

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
  void Run() {
    struct Case {
      Templates::ScriptKind kind;
      std::vector<Templates::ScriptKind> allowed;
      int choice_index;  // -1 keeps the fixed/default kind
      const char* name;
      bool open_after;
    };
    const std::vector<Case> cases = {
        {Templates::ScriptKind::kAngelScript,
         {Templates::ScriptKind::kAngelScript},
         -1,
         "PlayerMover",
         false},
        {Templates::ScriptKind::kCppBehaviour,
         {Templates::ScriptKind::kCppBehaviour},
         -1,
         "PlayerController",
         false},
        // The browser's all-kinds dialog: the selected choice decides.
        {Templates::ScriptKind::kCppSystem, {}, 2, "NetworkSystem", false},
        {Templates::ScriptKind::kCppEditorSystem,
         {Templates::ScriptKind::kCppEditorSystem},
         -1,
         "MyTool",
         false},
        // The checkbox on, with AngelScript (opening .as stays in the editor).
        {Templates::ScriptKind::kAngelScript,
         {Templates::ScriptKind::kAngelScript},
         -1,
         "OpenedScript",
         true},
    };

    for (const Case& item : cases) {
      pending_name_ = item.name;
      pending_open_ = item.open_after;
      pending_choice_index_ = item.choice_index;
      CallAfter([this]() { FillAndClose(); });
      auto request = ShowNewScriptDialog(frame_, *scene_, item.allowed);
      if (!request) {
        wxPrintf("FAIL: %s was cancelled\n", item.name);
        exit_code_ = 1;
        continue;
      }
      if (request->kind != item.kind || request->class_name != item.name ||
          request->open_after_create != item.open_after) {
        wxPrintf("FAIL: %s returned kind/name/checkbox mismatch\n", item.name);
        exit_code_ = 1;
        continue;
      }
      if (!scene_->CreateScript(request->kind, request->class_name,
                                request->open_after_create)) {
        wxPrintf("FAIL: %s was not created\n", item.name);
        exit_code_ = 1;
        continue;
      }
      const Templates::ScriptPaths paths = Templates::GetScriptPaths(
          scene_->GetProject()->GetRoot(), item.kind, item.name);
      if (!fs::is_regular_file(paths.source) ||
          (!paths.header.empty() && !fs::is_regular_file(paths.header))) {
        wxPrintf("FAIL: %s files are missing\n", item.name);
        exit_code_ = 1;
      }
    }

    // The last case had "open after creating" on; the editor's own script
    // editor gets the .as, and no external process is started for it.
    fs::path opened;
    if (!scene_->ConsumeScriptOpenRequest(&opened, nullptr) ||
        opened.filename() != "opened_script.as") {
      wxPrintf("FAIL: the opened script request was not recorded\n");
      exit_code_ = 1;
    }

    const bool remembered = scene_->Settings().new_script_kind ==
                            Templates::ScriptKind::kAngelScript;
    if (!remembered) {
      wxPrintf("FAIL: the last kind was not remembered\n");
      exit_code_ = 1;
    }
    wxPrintf("new script dialog: %s\n", exit_code_ == 0 ? "ok" : "failed");
  }

  void FillAndClose() {
    wxDialog* dialog = FindNewScriptDialog();
    if (!dialog) {
      wxPrintf("FAIL: the dialog did not appear\n");
      exit_code_ = 1;
      return;
    }
    if (auto* input = FindNamed<wxTextCtrl>(dialog, "script_class_name")) {
      input->SetValue(wxString::FromUTF8(pending_name_));
    }
    if (auto* checkbox = FindNamed<wxCheckBox>(dialog, "script_open_after")) {
      checkbox->SetValue(pending_open_);
    }
    if (pending_choice_index_ >= 0) {
      if (auto* choice = FindNamed<wxChoice>(dialog, "script_kind")) {
        choice->SetSelection(pending_choice_index_);
      }
    }
    dialog->EndModal(wxID_OK);
  }

  std::string pending_name_;
  bool pending_open_ = false;
  int pending_choice_index_ = -1;
  std::unique_ptr<EditorScene> scene_;
  wxFrame* frame_ = nullptr;
  fs::path config_dir_;
  std::string previous_xdg_;
  bool had_xdg_ = false;
  int exit_code_ = 0;
};

}  // namespace

wxIMPLEMENT_APP(NewScriptSmokeApp);
