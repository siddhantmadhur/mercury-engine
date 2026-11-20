// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "extension.h"

#include <utility>

#include "console.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "editor_settings.h"

namespace Mercury::EditorUI {

namespace {

// Parses the leading "major.minor" of a version string. Returns false when the
// string does not start with two dot-separated integers.
bool ParseMajorMinor(std::string_view version, int& major, int& minor) {
  size_t i = 0;
  auto parse_int = [&](int& out) {
    const size_t start = i;
    while (i < version.size() && version[i] >= '0' && version[i] <= '9') {
      out = out * 10 + (version[i] - '0');
      i++;
    }
    return i > start;
  };
  major = 0;
  minor = 0;
  if (!parse_int(major)) return false;
  if (i >= version.size() || version[i] != '.') return false;
  i++;
  return parse_int(minor);
}

}  // namespace

bool IsExtensionVersionCompatible(std::string_view module_version,
                                  std::string_view engine_version) {
  int module_major = 0;
  int module_minor = 0;
  int engine_major = 0;
  int engine_minor = 0;
  if (!ParseMajorMinor(module_version, module_major, module_minor) ||
      !ParseMajorMinor(engine_version, engine_major, engine_minor)) {
    return module_version == engine_version;
  }
  return module_major == engine_major && module_minor == engine_minor;
}

EditorExtensionRegistry& EditorExtensionRegistry::Instance() {
  static EditorExtensionRegistry registry;
  return registry;
}

void EditorExtensionRegistry::AddPanel(std::string module,
                                       EditorPanelInfo info) {
  panels_.push_back({std::move(module), std::move(info)});
}

void EditorExtensionRegistry::AddMenuItem(std::string module,
                                          EditorMenuItemInfo info) {
  menu_items_.push_back({std::move(module), std::move(info)});
}

void EditorExtensionRegistry::AddInspector(std::string module,
                                           EditorInspectorInfo info) {
  inspectors_.push_back({std::move(module), std::move(info)});
}

void EditorExtensionRegistry::AddProjectMenuItem(
    std::string module, EditorProjectMenuItemInfo info) {
  project_menu_items_.push_back({std::move(module), std::move(info)});
}

void EditorExtensionRegistry::AddViewportTool(std::string module,
                                              EditorViewportToolInfo info) {
  viewport_tools_.push_back({std::move(module), std::move(info)});
}

void EditorExtensionRegistry::RemoveModule(std::string_view module) {
  auto matches = [module](const auto& entry) { return entry.module == module; };
  std::erase_if(panels_, matches);
  std::erase_if(menu_items_, matches);
  std::erase_if(inspectors_, matches);
  std::erase_if(project_menu_items_, matches);
  std::erase_if(viewport_tools_, matches);
}

void EditorExtensionRegistry::Clear() {
  panels_.clear();
  menu_items_.clear();
  inspectors_.clear();
  project_menu_items_.clear();
  viewport_tools_.clear();
}

const EditorInspectorInfo* EditorExtensionRegistry::FindInspector(
    std::string_view component) const {
  for (const InspectorEntry& entry : inspectors_) {
    if (entry.info.component == component) return &entry.info;
  }
  return nullptr;
}

size_t EditorExtensionRegistry::Count() const {
  return panels_.size() + menu_items_.size() + inspectors_.size() +
         project_menu_items_.size() + viewport_tools_.size();
}

EditorContext::EditorContext(std::string module_id, EditorFrame* frame,
                             EditorScene* scene, EditorSettings* settings,
                             Console* console, wxAuiManager* aui_manager)
    : registry_(&EditorExtensionRegistry::Instance()),
      module_id_(std::move(module_id)),
      frame_(frame),
      scene_(scene),
      settings_(settings),
      console_(console),
      aui_manager_(aui_manager) {}

EditorContext::~EditorContext() {
  if (registry_) registry_->RemoveModule(module_id_);
  AssetImporterRegistry::Instance().UnregisterOwner(module_id_);
}

wxAuiManager& EditorContext::GetAuiManager() {
  wxASSERT(aui_manager_);
  return *aui_manager_;
}

EditorSettings& EditorContext::GetSettings() {
  wxASSERT(settings_);
  return *settings_;
}

Console& EditorContext::GetConsole() {
  wxASSERT(console_);
  return *console_;
}

void EditorContext::AddPanel(EditorPanelInfo info) {
  registry_->AddPanel(module_id_, std::move(info));
}

void EditorContext::AddMenuItem(EditorMenuItemInfo info) {
  registry_->AddMenuItem(module_id_, std::move(info));
}

void EditorContext::AddInspector(EditorInspectorInfo info) {
  registry_->AddInspector(module_id_, std::move(info));
}

void EditorContext::AddProjectMenuItem(EditorProjectMenuItemInfo info) {
  registry_->AddProjectMenuItem(module_id_, std::move(info));
}

void EditorContext::AddViewportTool(EditorViewportToolInfo info) {
  registry_->AddViewportTool(module_id_, std::move(info));
}

void EditorContext::AddAssetImporter(AssetImporterInfo info) {
  AssetImporterRegistry::Instance().RegisterOwned(module_id_, std::move(info));
}

void EditorContext::TrackWindow(wxWindow* window) {
  if (window) tracked_windows_.push_back(window);
}

}  // namespace Mercury::EditorUI
