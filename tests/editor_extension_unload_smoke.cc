// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// mercury_editor_unload_smoke <project.mproj>
//
// Regression test for unloading an editor module whose custom inspector is
// showing: after the module's registry entries are removed and the frame
// removes its UI, the inspector window it created must be gone before the
// module's library would be dlclosed. Like editor_smoke, this needs a real
// wxWidgets frame, so it is a standalone smoke executable rather than a
// gtest case.

#include <wx/weakref.h>
#include <wx/wx.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "application/default_application.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "extension.h"
#include "os/filesystem.h"

namespace {

using namespace Mercury;
using namespace Mercury::EditorUI;

wxWeakRef<wxWindow> g_inspector_window;

class UnloadSmokeApp : public wxApp {
 public:
  bool OnInit() override {
    wxInitAllImageHandlers();

    std::filesystem::path project = std::filesystem::path(MERCURY_SOURCE_DIR) /
                                    "examples/cpp_hello/cpp_hello.mproj";
    for (int i = 1; i < argc; ++i) {
      const std::string arg = wxString(argv[i]).ToStdString();
      if (!arg.starts_with("--")) project = os::UserPath(arg);
    }

    application_ =
        std::make_unique<DefaultApplication>("Mercury unload smoke", 900, 600);
    application_->EnableHeadlessMode(true);
    auto scene = std::make_unique<EditorScene>(project);
    scene_ = scene.get();
    application_->SetGameScene(std::move(scene));
    if (!application_->StartHosted()) return false;

    frame_ = new EditorFrame(*scene_, []() {}, [this]() { ExitMainLoop(); });
    frame_->Show(true);
    SetTopWindow(frame_);

    context_ = std::make_unique<EditorContext>(
        "unload-smoke", frame_, scene_, &scene_->Settings(),
        &scene_->ConsoleRef(), &frame_->GetAuiManager());
    context_->AddInspector(
        {"Transform", [](wxWindow* parent, EditorScene&, MapNode&) {
           auto* window = new wxPanel(parent, wxID_ANY, wxDefaultPosition,
                                      wxSize(120, 24));
           g_inspector_window = window;
           return window;
         }});

    // Select a node so the Inspector builds the custom row, then sync it.
    if (MapNode* root = scene_->GetMap().GetRoot()) {
      MapNode* target = root;
      if (!root->children.empty()) target = root->children.front().get();
      scene_->Select(target);
    }
    frame_->SyncFromController(0.016f);

    CallAfter([this]() {
      CheckUnload();
      ExitMainLoop();
    });
    return true;
  }

  int OnExit() override {
    context_.reset();
    frame_ = nullptr;
    scene_ = nullptr;
    application_.reset();
    return exit_code_;
  }

 private:
  void CheckUnload() {
    const bool created = static_cast<bool>(g_inspector_window);
    // Simulate EditorExtensionLoader::UnloadAll's ordering: registry entries
    // first, then the frame's UI (which synchronously rebuilds the Inspector),
    // then the deferred wx deletions.
    EditorExtensionRegistry::Instance().RemoveModule("unload-smoke");
    frame_->RemoveExtensionUI("unload-smoke");
    ProcessPendingEvents();
    ProcessIdle();
    const bool destroyed = !g_inspector_window;

    wxPrintf("custom inspector created: %s, destroyed: %s\n",
             created ? "yes" : "no", destroyed ? "yes" : "no");
    exit_code_ = created && destroyed ? 0 : 1;
  }

  std::unique_ptr<DefaultApplication> application_;
  EditorScene* scene_ = nullptr;
  EditorFrame* frame_ = nullptr;
  std::unique_ptr<EditorContext> context_;
  int exit_code_ = 0;
};

}  // namespace

wxIMPLEMENT_APP(UnloadSmokeApp);
