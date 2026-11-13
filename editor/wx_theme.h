/*
 * wx_theme.h
 *
 * Dark Source 2 / IOI Glacier visual theme, AUI dock/tab/toolbar art providers,
 * reusable retained-mode inspector controls (CollapsibleSection, DragNumberCtrl,
 * Vec2Field, Vec3Field, ColorSwatchCtrl), and RGBA8 -> wxBitmap conversion.
 */
#ifndef MERCURY_EDITOR_WX_THEME_H
#define MERCURY_EDITOR_WX_THEME_H

#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <wx/aui/tabart.h>
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

// Source 2 Hammer / IOI Glacier inspired dark neutral studio palette.
namespace Colors {
inline const wxColour kBgCanvas(24, 26, 29);        // #181A1D
inline const wxColour kBgPanel(32, 34, 38);         // #202226
inline const wxColour kBgHeader(40, 43, 48);        // #282B30
inline const wxColour kBgControl(45, 49, 56);       // #2D3138
inline const wxColour kBgControlHover(55, 60, 69);  // #373C45
inline const wxColour kBgInput(27, 29, 33);         // #1B1D21
inline const wxColour kBgSelected(44, 75, 115);     // #2C4B73
inline const wxColour kBgRowAlt(36, 39, 44);        // #24272C
inline const wxColour kBorder(60, 65, 74);          // #3C414A
inline const wxColour kBorderStrong(78, 84, 96);    // #4E5460
inline const wxColour kTextPrimary(228, 231, 236);  // #E4E7EC
inline const wxColour kTextSecondary(154, 161, 174);// #9AA1AE
inline const wxColour kTextMuted(109, 116, 130);    // #6D7482
inline const wxColour kAccentAmber(217, 130, 43);   // #D9822B (Source 2)
inline const wxColour kAccentBlue(76, 142, 218);    // #4C8EDA (Glacier)
inline const wxColour kAccentGreen(62, 155, 86);    // #3E9B56
inline const wxColour kAccentRed(201, 74, 74);      // #C94A4A
inline const wxColour kAxisX(224, 85, 69);          // #E05545
inline const wxColour kAxisY(104, 184, 72);         // #68B848
inline const wxColour kAxisZ(74, 143, 226);         // #4A8FE2
}  // namespace Colors

void ApplyDarkPalette(wxWindow* window);
void StyleButton(wxButton* btn, bool primary = false,
                 const wxColour& accent = Colors::kBgControl);
void StyleTextCtrl(wxTextCtrl* ctrl);
void StyleChoice(wxChoice* choice);
wxFont MonospaceFont(int point_delta = 0);
wxFont HeaderFont(int point_delta = 0);

// Fast RGBA8 -> wxBitmap conversion. Reuses internal wxImage buffers via caller
// helper if desired; alpha channel is only populated when `with_alpha` is true.
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

// ------------------------------------------------------------------ AUI Art ---

class DarkDockArt : public wxAuiDefaultDockArt {
 public:
  DarkDockArt();
  void DrawBorder(wxDC& dc, wxWindow* window, const wxRect& rect,
                  wxAuiPaneInfo& pane) override;
  void DrawCaption(wxDC& dc, wxWindow* window, const wxString& text,
                   const wxRect& rect, wxAuiPaneInfo& pane) override;
  void DrawSash(wxDC& dc, wxWindow* window, int orientation,
                const wxRect& rect) override;
  void DrawBackground(wxDC& dc, wxWindow* window, int orientation,
                      const wxRect& rect) override;
};

class DarkTabArt : public wxAuiGenericTabArt {
 public:
  DarkTabArt();
  wxAuiTabArt* Clone() override;
  void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
  void DrawTab(wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& pane,
               const wxRect& in_rect, int close_button_state, wxRect* out_tab_rect,
               wxRect* out_button_rect, int* x_extent) override;
};

class DarkToolBarArt : public wxAuiDefaultToolBarArt {
 public:
  DarkToolBarArt();
  wxAuiToolBarArt* Clone() override;
  void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
  void DrawPlainBackground(wxDC& dc, wxWindow* wnd,
                           const wxRect& rect) override;
  void DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item,
                  const wxRect& rect) override;
  void DrawSeparator(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
};

// --------------------------------------------------------- Custom Controls ---

// Source 2 / Glacier style collapsible inspector card.
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

// Drag-scrubbable scalar control with color-coded axis/label handle + text entry.
class DragNumberCtrl : public wxPanel {
 public:
  DragNumberCtrl(wxWindow* parent, const wxString& label = wxEmptyString,
                 const wxColour& tag_color = Colors::kBorderStrong,
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
