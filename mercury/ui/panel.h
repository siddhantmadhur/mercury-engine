/*
 * panel.h
 *
 * A Panel owns one tree of VisualElements and everything needed to run it:
 * styles, layout, input, focus, bindings, transitions and drawing. Games get
 * panels from UIDocument components (one per PanelSettings asset, as in
 * Unity); the editor's UI Builder makes one for its canvas.
 *
 *   Panel panel(&resources);
 *   VisualTreeAsset::Parse(xml)->CloneTree(panel.visualTree(), resources);
 *   // every frame:
 *   panel.DispatchInput(input);
 *   panel.Update(dt, {width, height});
 *   renderer.Draw(panel.drawData());
 */
#ifndef MERCURY_UI_PANEL_H
#define MERCURY_UI_PANEL_H

#include <memory>
#include <string>
#include <vector>

#include "ui/draw_data.h"
#include "ui/input.h"
#include "ui/resources.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

enum class PanelScaleMode : uint8_t {
  kConstantPixelSize,
  kConstantPhysicalSize,
  kScaleWithScreenSize,
};
enum class PanelScreenMatchMode : uint8_t {
  kMatchWidthOrHeight,
  kShrink,
  kExpand,
};

/**
 * @brief How a panel scales to the screen and which theme it uses
 * (Unity's PanelSettings)
 */
struct PanelSettings {
  PanelScaleMode scale_mode = PanelScaleMode::kConstantPixelSize;
  float scale = 1.0f;
  glm::vec2 reference_resolution{1200.0f, 800.0f};
  PanelScreenMatchMode screen_match_mode = PanelScreenMatchMode::kMatchWidthOrHeight;
  float match = 0.0f;  // 0 = width, 1 = height
  float reference_dpi = 96.0f;
  float fallback_dpi = 96.0f;
  // "" uses the engine's default theme.
  std::string theme_style_sheet;
  float sort_order = 0.0f;
};

class FocusController {
 public:
  explicit FocusController(Panel* panel) : panel_(panel) {}
  VisualElement* focusedElement() const { return focused_; }
  void SwitchFocus(VisualElement* element);
  // Next/previous element in tab order (tabIndex, then tree order).
  VisualElement* FindNext(VisualElement* from, bool forward) const;
  // Nearest focusable element in a direction, for gamepad/arrow navigation.
  VisualElement* FindInDirection(VisualElement* from,
                                 NavigationDirection direction) const;
  void OnDetached(VisualElement* element);

 private:
  Panel* panel_;
  VisualElement* focused_ = nullptr;
};

class Panel {
 public:
  explicit Panel(UIResources* resources = nullptr);
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

  // The root of the tree; fills the panel.
  VisualElement* visualTree() const { return root_.get(); }
  const PanelSettings& settings() const { return settings_; }
  void SetSettings(PanelSettings settings);
  UIResources& resources() { return *resources_; }

  void DispatchInput(const UIInputFrame& input);
  /**
   * @param target_size render target size in pixels
   * @param dpi_scale display scale (1 = 96 DPI), for constant physical size
   */
  void Update(double dt_seconds, glm::vec2 target_size, float dpi_scale = 1.0f);
  const UIDrawData& drawData() const { return draw_data_; }

  // Pixels per panel unit, from the settings and the target size.
  float scale() const { return scale_; }
  glm::vec2 size() const { return size_; }  // in panel units
  double time() const { return time_; }     // seconds since creation

  VisualElement* Pick(glm::vec2 panel_point) const;
  // Every element under the point, topmost first.
  std::vector<VisualElement*> PickAll(glm::vec2 panel_point) const;
  // Whether the last pointer position is over an element that blocks input.
  bool IsPointerOverUI() const { return pointer_over_ui_; }
  VisualElement* hoveredElement() const { return hovered_; }

  FocusController& focusController() { return focus_; }
  VisualElement* focusedElement() const { return focus_.focusedElement(); }

  void CapturePointer(VisualElement* element, int pointer_id);
  void ReleasePointer(VisualElement* element, int pointer_id);
  VisualElement* GetCapturingElement(int pointer_id) const;

  // Sends an event through the tree: trickle down, at target, bubble up.
  void Dispatch(EventBase& event, VisualElement* target,
                bool skip_disabled = false);

  UIClipboard clipboard;

  // Forces a restyle of everything (e.g. after a sheet was edited).
  void RestyleAll();
  void MarkStyleDirty() { style_dirty_ = true; }
  void MarkLayoutDirty() { layout_dirty_ = true; }
  void MarkRepaint() { repaint_ = true; }

  // Used by elements.
  void OnElementDetached(VisualElement* element);
  void OnTextInputFocus(bool active) { text_input_active_ = active; }
  bool textInputActive() const { return text_input_active_; }

 private:
  void UpdateScale(glm::vec2 target_size, float dpi_scale);
  void UpdateStyles();
  void UpdateLayout();
  void Repaint();
  void UpdateHover(glm::vec2 point, bool inside, KeyModifiers mods);
  template <typename E>
  void FillPointer(E& event, glm::vec2 point, int button, KeyModifiers mods);
  void HandleNavigationKey(const UIInputEvent& event);
  bool YGNodeIsDirtyRoot() const;

  std::unique_ptr<UIResources> owned_resources_;
  UIResources* resources_;
  std::unique_ptr<VisualElement> root_;
  PanelSettings settings_;
  FocusController focus_{this};
  UIDrawData draw_data_;

  float scale_ = 1.0f;
  glm::vec2 size_{0.0f};
  glm::vec2 target_size_{0.0f};
  double time_ = 0.0;
  uint64_t resources_version_ = 0;
  uint64_t atlas_resizes_ = 0;
  bool style_dirty_ = true;
  bool layout_dirty_ = true;
  bool repaint_ = true;

  // Pointer state.
  glm::vec2 pointer_{-1.0f};
  bool pointer_known_ = false;
  bool pointer_over_ui_ = false;
  int pressed_buttons_ = 0;
  VisualElement* hovered_ = nullptr;
  std::vector<VisualElement*> hover_chain_;  // hovered and its ancestors
  VisualElement* capture_ = nullptr;
  VisualElement* press_target_[3] = {nullptr, nullptr, nullptr};
  double last_click_time_ = -10.0;
  glm::vec2 last_click_pos_{0.0f};
  int click_count_ = 0;
  bool text_input_active_ = false;
  std::string fallback_clipboard_;
};

// Scale from the panel settings for a target size (Unity's rules).
float ComputePanelScale(const PanelSettings& settings, glm::vec2 target_size,
                        float dpi_scale);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_PANEL_H
