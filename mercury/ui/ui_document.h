/*
 * ui_document.h
 *
 * UIDocument shows a UXML file on screen, like Unity's UIDocument
 * component. Documents that use the same PanelSettings asset share a panel
 * and are stacked by sort_order.
 *
 *   [component type="UIDocument" node="HUD"]
 *   source_asset = Resource("res://ui/hud.uxml")
 *   panel_settings = Resource("res://ui/hud_panel.masset")
 *
 * In a script:
 *
 *   void OnStart() override {
 *     auto* root = GetDocument()->rootVisualElement();
 *     root->Q<Button>("play")->clicked += [this] { StartGame(); };
 *     root->SetDataSource(AsDataSource());
 *   }
 */
#ifndef MERCURY_UI_UI_DOCUMENT_H
#define MERCURY_UI_UI_DOCUMENT_H

#include <glm/glm.hpp>

#include "scripting/reflection.h"
#include "ui/panel.h"

namespace Mercury {

struct UIDocument {
  AssetRef source_asset;    // a .uxml file
  AssetRef panel_settings;  // a PanelSettings .masset; empty uses defaults
  float sort_order = 0.0f;  // higher draws on top within the panel

  void Reflect(Reflector& r) {
    r.Field("source_asset", source_asset, "UXML").Tooltip("The .uxml file to show");
    r.Field("panel_settings", panel_settings, "PanelSettings")
        .Tooltip("Scaling and theme; documents sharing one share a panel");
    r.Field("sort_order", sort_order).Tooltip("Higher draws on top");
  }

  // The document's root (a TemplateContainer filling the panel), or null in
  // the editor and when the UXML failed to load.
  UI::VisualElement* rootVisualElement() const { return root; }
  UI::Panel* runtimePanel() const { return panel; }

  // Set when the document is shown.
  UI::VisualElement* root = nullptr;
  UI::Panel* panel = nullptr;
};

/**
 * @brief Unity's PanelSettings as an asset
 */
struct PanelSettingsAsset {
  UI::PanelScaleMode scale_mode = UI::PanelScaleMode::kConstantPixelSize;
  float scale = 1.0f;
  glm::vec2 reference_resolution = glm::vec2(1200.0f, 800.0f);
  UI::PanelScreenMatchMode screen_match_mode =
      UI::PanelScreenMatchMode::kMatchWidthOrHeight;
  float match = 0.0f;
  float reference_dpi = 96.0f;
  AssetRef theme_style_sheet;
  float sort_order = 0.0f;

  void Reflect(Reflector& r) {
    r.Enum("scale_mode", scale_mode,
           {"ConstantPixelSize", "ConstantPhysicalSize", "ScaleWithScreenSize"})
        .Tooltip("How the UI grows with the screen");
    r.Field("scale", scale).Tooltip("Multiplies the scale from scale_mode");
    r.Field("reference_resolution", reference_resolution)
        .Tooltip("ScaleWithScreenSize: the size the UI was designed for");
    r.Enum("screen_match_mode", screen_match_mode,
           {"MatchWidthOrHeight", "Shrink", "Expand"});
    r.Field("match", match).Tooltip("0 = match width, 1 = match height");
    r.Field("reference_dpi", reference_dpi).Tooltip("ConstantPhysicalSize only");
    r.Field("theme_style_sheet", theme_style_sheet, "StyleSheet")
        .Tooltip("A .uss theme; empty uses the default runtime theme");
    r.Field("sort_order", sort_order).Tooltip("Panels with higher values draw on top");
  }

  UI::PanelSettings ToSettings() const {
    UI::PanelSettings s;
    s.scale_mode = scale_mode;
    s.scale = scale;
    s.reference_resolution = reference_resolution;
    s.screen_match_mode = screen_match_mode;
    s.match = match;
    s.reference_dpi = reference_dpi;
    s.theme_style_sheet = theme_style_sheet.path;
    s.sort_order = sort_order;
    return s;
  }
};

}  // namespace Mercury

#endif  // MERCURY_UI_UI_DOCUMENT_H
