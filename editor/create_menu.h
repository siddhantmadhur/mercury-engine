/*
 * create_menu.h
 *
 * Shared wxMenu builder for creating MapNodes / Presets in the scene.
 */
#ifndef MERCURY_EDITOR_CREATE_MENU_H
#define MERCURY_EDITOR_CREATE_MENU_H

#include <wx/wx.h>

#include <functional>
#include <vector>

#include "editor_scene.h"

namespace Mercury::EditorUI {

struct CreatePresetEntry {
  const char* label;
  EditorScene::Preset preset;
  bool top_level;
};

inline void PopulateCreateNodeMenu(
    wxMenu* menu, EditorScene& scene, MapNode* parent,
    const std::function<void(MapNode*)>& on_created = nullptr,
    wxEvtHandler* handler = nullptr) {
  wxEvtHandler* sink = handler ? handler : static_cast<wxEvtHandler*>(menu);
  auto add_preset = [&](wxMenu* target, const char* label,
                        EditorScene::Preset preset) {
    wxMenuItem* item = target->Append(wxID_ANY, label);
    sink->Bind(
        wxEVT_MENU,
        [&scene, parent, preset, on_created](wxCommandEvent&) {
          MapNode* created = scene.CreatePreset(parent, preset);
          if (created && on_created) on_created(created);
        },
        item->GetId());
  };

  add_preset(menu, "Empty Node", EditorScene::Preset::kEmpty);
  menu->AppendSeparator();

  auto* objects3d = new wxMenu();
  add_preset(objects3d, "Cube", EditorScene::Preset::kCube);
  add_preset(objects3d, "Sphere", EditorScene::Preset::kSphere);
  add_preset(objects3d, "Capsule", EditorScene::Preset::kCapsule);
  add_preset(objects3d, "Cylinder", EditorScene::Preset::kCylinder);
  add_preset(objects3d, "Plane", EditorScene::Preset::kPlane);
  add_preset(objects3d, "Quad", EditorScene::Preset::kQuad);
  menu->AppendSubMenu(objects3d, "3D Object");

  auto* lights = new wxMenu();
  add_preset(lights, "Directional Light",
             EditorScene::Preset::kDirectionalLight);
  add_preset(lights, "Point Light", EditorScene::Preset::kPointLight);
  add_preset(lights, "Area Light", EditorScene::Preset::kAreaLight);
  add_preset(lights, "Light Probe Volume",
             EditorScene::Preset::kLightProbeVolume);
  menu->AppendSubMenu(lights, "Light");

  add_preset(menu, "2D Sprite", EditorScene::Preset::kSprite);
  add_preset(menu, "Camera", EditorScene::Preset::kCamera);
}

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_CREATE_MENU_H
