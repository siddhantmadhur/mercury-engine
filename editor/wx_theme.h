/*
 * wx_theme.h
 *
 * Shared fonts, reusable retained-mode inspector controls (CollapsibleSection,
 * DragNumberCtrl, Vec2Field, Vec3Field, ColorSwatchCtrl), RGBA8 -> wxBitmap
 * conversion, and WxOverlayCanvas for viewport overlays.
 */
#ifndef MERCURY_EDITOR_WX_THEME_H
#define MERCURY_EDITOR_WX_THEME_H

#include <wx/graphics.h>
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "overlay_canvas.h"

namespace Mercury::EditorUI {

// Axis indicator colors for 3D gizmos / Vec2 / Vec3 fields.
namespace Colors {
inline const wxColour kAxisX(224, 85, 69);
inline const wxColour kAxisY(104, 184, 72);
inline const wxColour kAxisZ(74, 143, 226);
}  // namespace Colors

wxFont MonospaceFont(int point_delta = 0);
wxFont HeaderFont(int point_delta = 0);

// Fast RGBA8 -> wxBitmap conversion.
wxBitmap RgbaToWxBitmap(const uint8_t* rgba, int width, int height,
                        bool with_alpha = false);

void DrawCheckerboard(wxGraphicsContext* gc, double x, double y, double w,
                      double h, int tile = 10);

// wxGraphicsContext-backed OverlayCanvas for Scene Viewport & gizmo overlays.
class WxOverlayCanvas : public OverlayCanvas {
 public:
  WxOverlayCanvas(wxGraphicsContext* gc, const wxFont& font);

  void AddLine(glm::vec2 a, glm::vec2 b, OverlayColor color,
               float thickness = 1.0f) override;
  void AddCircle(glm::vec2 center, float radius, OverlayColor color,
                 float thickness = 1.0f) override;
  void AddCircleFilled(glm::vec2 center, float radius,
                       OverlayColor color) override;
  void AddRect(glm::vec2 min, glm::vec2 max, OverlayColor color,
               float rounding = 0.0f, float thickness = 1.0f) override;
  void AddRectFilled(glm::vec2 min, glm::vec2 max, OverlayColor color,
                     float rounding = 0.0f) override;
  void AddQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
               OverlayColor color, float thickness = 1.0f) override;
  void AddQuadFilled(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                     OverlayColor color) override;
  void AddTriangleFilled(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                         OverlayColor color) override;
  void AddText(glm::vec2 pos, OverlayColor color,
               const std::string& text) override;
  glm::vec2 MeasureText(const std::string& text) const override;

 private:
  wxGraphicsContext* gc_;
  wxFont font_;
};

// --------------------------------------------------------- Custom Controls ---

// Collapsible inspector card.
class CollapsibleSection : public wxPanel {
 public:
  CollapsibleSection(wxWindow* parent, const wxString& title,
                     bool expanded = true, const wxString& badge = wxEmptyString,
                     std::function<void(const wxPoint&)> on_context_menu = nullptr,
                     std::function<void(bool)> on_toggle = nullptr);

  wxPanel* Body() { return body_; }
  wxBoxSizer* BodySizer() { return body_sizer_; }
  void SetTitle(const wxString& title);
  void SetBadge(const wxString& badge);
  void SetExpanded(bool expanded, bool notify = false);
  bool IsExpanded() const { return expanded_; }

  // Call after populating BodySizer() so initial sizing matches expanded state.
  void FinishBuild();

 private:
  void OnPaintHeader(wxPaintEvent& evt);
  void OnHeaderClick(wxMouseEvent& evt);
  void OnHeaderRightClick(wxMouseEvent& evt);
  void UpdateLayout();

  wxPanel* header_ = nullptr;
  wxPanel* body_ = nullptr;
  wxBoxSizer* body_sizer_ = nullptr;
  wxString title_;
  wxString badge_;
  bool expanded_ = true;
  bool hovered_ = false;
  std::function<void(const wxPoint&)> on_context_;
  std::function<void(bool)> on_toggle_;
};

// Drag-scrubbable scalar control with handle + native text entry.
class DragNumberCtrl : public wxPanel {
 public:
  DragNumberCtrl(wxWindow* parent, const wxString& label = wxEmptyString,
                 const wxColour& tag_color = wxNullColour,
                 double value = 0.0, double step = 0.05,
                 bool integer_only = false, int precision = 3);

  void SetValue(double v);
  double GetValue() const { return value_; }
  void SetCallbacks(std::function<void()> on_begin,
                    std::function<void(double)> on_change,
                    std::function<void()> on_end);

  bool IsEditingText() const;

 private:
  void SyncText();
  void CommitText();
  void OnTagPaint(wxPaintEvent& evt);
  void OnTagMouse(wxMouseEvent& evt);
  void OnCaptureLost(wxMouseCaptureLostEvent& evt);

  wxPanel* tag_ = nullptr;
  wxTextCtrl* text_ = nullptr;
  wxString label_;
  wxColour tag_color_;
  double value_ = 0.0;
  double drag_start_val_ = 0.0;
  double step_ = 0.05;
  bool integer_only_ = false;
  int precision_ = 3;
  bool dragging_ = false;
  int drag_start_x_ = 0;
  std::function<void()> on_begin_;
  std::function<void(double)> on_change_;
  std::function<void()> on_end_;
};

class Vec2Field : public wxPanel {
 public:
  Vec2Field(wxWindow* parent, glm::vec2 value, double step = 0.05);
  void SetValue(glm::vec2 v);
  glm::vec2 GetValue() const;
  void SetCallbacks(std::function<void()> on_begin,
                    std::function<void(glm::vec2)> on_change,
                    std::function<void()> on_end);
  bool IsEditingText() const;

 private:
  DragNumberCtrl* x_ = nullptr;
  DragNumberCtrl* y_ = nullptr;
};

class Vec3Field : public wxPanel {
 public:
  Vec3Field(wxWindow* parent, glm::vec3 value, double step = 0.05);
  void SetValue(glm::vec3 v);
  glm::vec3 GetValue() const;
  void SetCallbacks(std::function<void()> on_begin,
                    std::function<void(glm::vec3)> on_change,
                    std::function<void()> on_end);
  bool IsEditingText() const;

 private:
  DragNumberCtrl* x_ = nullptr;
  DragNumberCtrl* y_ = nullptr;
  DragNumberCtrl* z_ = nullptr;
};

class ColorSwatchCtrl : public wxPanel {
 public:
  ColorSwatchCtrl(wxWindow* parent, glm::vec4 rgba);
  void SetValue(glm::vec4 rgba);
  glm::vec4 GetValue() const { return rgba_; }
  void SetOnChange(std::function<void(glm::vec4)> on_change);

 private:
  void OnPaint(wxPaintEvent& evt);
  void OnClick(wxMouseEvent& evt);

  glm::vec4 rgba_{1.0f, 1.0f, 1.0f, 1.0f};
  std::function<void(glm::vec4)> on_change_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_WX_THEME_H
