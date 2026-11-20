// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A minimal editor system: a dockable panel and a menu item, both registered
// through EditorContext. Build it with `cmake --build <build> --target
// cpp_hello_systems`; the editor loads it when cpp_hello is open and unloads
// it when the project closes or Project > Reload Systems is used.

#include "hello_panel.h"

#include <string>

#include "console.h"
#include "editor_scene.h"
#include "extension.h"
#include "project/map.h"

namespace Mercury::EditorUI {

HelloPanel::HelloPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent), scene_(scene) {
  auto* sizer = new wxBoxSizer(wxVERTICAL);

  auto* label = new wxStaticText(this, wxID_ANY, "Entities in the open map:");
  sizer->Add(label, 0, wxALL, 8);

  list_ = new wxListBox(this, wxID_ANY);
  sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto* refresh = new wxButton(this, wxID_ANY, "Refresh");
  refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshEntities(); });
  sizer->Add(refresh, 0, wxALL, 8);

  SetSizer(sizer);
  RefreshEntities();
}

void HelloPanel::RefreshEntities() {
  list_->Clear();
  scene_.GetMap().Visit([this](const MapNode& node) {
    list_->Append(wxString::FromUTF8(node.GetPath()));
  });
}

namespace {

class HelloExtension : public EditorExtension {
 public:
  void OnLoad(EditorContext& context) override {
    scene_ = context.GetScene();
    console_ = &context.GetConsole();
    context.AddPanel({.id = "hello.panel",
                      .title = "Hello",
                      .create = [this](wxWindow* parent) {
                        return new HelloPanel(parent, *scene_);
                      }});
    context.AddMenuItem({.menu = "Extensions",
                         .label = "Hello: Log Entity Count",
                         .callback = [this] { LogEntityCount(); }});
  }

  void OnUnload() override {
    scene_ = nullptr;
    console_ = nullptr;
  }

 private:
  void LogEntityCount() {
    if (!scene_ || !console_) return;
    size_t count = 0;
    scene_->GetMap().Visit([&count](const MapNode&) { count++; });
    console_->Info("Hello system: " + std::to_string(count) + " entities");
  }

  EditorScene* scene_ = nullptr;
  Console* console_ = nullptr;
};

}  // namespace

MERCURY_EDITOR_EXTENSION(HelloExtension)

}  // namespace Mercury::EditorUI
