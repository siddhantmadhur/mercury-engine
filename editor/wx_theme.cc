#include "wx_theme.h"

#include <wx/colordlg.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace Mercury::EditorUI {

namespace {
wxColour ToWx(OverlayColor c) { return wxColour(c.r, c.g, c.b, c.a); }

uint32_t PackRgba(const wxColour& c) {
  return (static_cast<uint32_t>(c.Red()) << 24) |
         (static_cast<uint32_t>(c.Green()) << 16) |
         (static_cast<uint32_t>(c.Blue()) << 8) |
         static_cast<uint32_t>(c.Alpha());
}

uint32_t PackRgba(OverlayColor c) {
  return (static_cast<uint32_t>(c.r) << 24) |
         (static_cast<uint32_t>(c.g) << 16) |
         (static_cast<uint32_t>(c.b) << 8) |
         static_cast<uint32_t>(c.a);
}
}  // namespace

wxFont MonospaceFont(int point_delta) {
  wxFont font(wxFontInfo(9 + point_delta).Family(wxFONTFAMILY_TELETYPE));
  return font;
}

wxFont HeaderFont(int point_delta) {
  wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
  font.SetWeight(wxFONTWEIGHT_BOLD);
  if (point_delta != 0) font.SetPointSize(std::max(7, font.GetPointSize() + point_delta));
  return font;
}

wxBitmap RgbaToWxBitmap(const uint8_t* rgba, int width, int height,
                        bool with_alpha) {
  if (!rgba || width <= 0 || height <= 0) return wxNullBitmap;
  const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
  unsigned char* rgb = static_cast<unsigned char*>(std::malloc(pixel_count * 3));
  unsigned char* alpha =
      with_alpha ? static_cast<unsigned char*>(std::malloc(pixel_count)) : nullptr;
  if (!rgb || (with_alpha && !alpha)) {
    std::free(rgb);
    std::free(alpha);
    return wxNullBitmap;
  }
  for (size_t i = 0; i < pixel_count; ++i) {
    rgb[i * 3 + 0] = rgba[i * 4 + 0];
    rgb[i * 3 + 1] = rgba[i * 4 + 1];
    rgb[i * 3 + 2] = rgba[i * 4 + 2];
    if (alpha) alpha[i] = rgba[i * 4 + 3];
  }
  wxImage img(width, height, rgb, /*static_data=*/false);
  if (alpha) img.SetAlpha(alpha, /*static_data=*/false);
  return wxBitmap(img);
}

void UpdateBitmapFromRgba(wxBitmap& bitmap, const uint8_t* rgba, int width,
                          int height, bool with_alpha) {
  if (!rgba || width <= 0 || height <= 0) {
    bitmap = wxNullBitmap;
    return;
  }

  if (!with_alpha) {
    static thread_local std::vector<unsigned char> rgb_scratch;
    const size_t pixel_count =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    rgb_scratch.resize(pixel_count * 3);
    unsigned char* dst = rgb_scratch.data();
    for (size_t i = 0; i < pixel_count; ++i) {
      dst[i * 3 + 0] = rgba[i * 4 + 0];
      dst[i * 3 + 1] = rgba[i * 4 + 1];
      dst[i * 3 + 2] = rgba[i * 4 + 2];
    }
    wxImage img(width, height, dst, /*static_data=*/true);
    bitmap = wxBitmap(img);
    return;
  }

  bitmap = RgbaToWxBitmap(rgba, width, height, with_alpha);
}

void DrawCheckerboard(wxGraphicsContext* gc, double x, double y, double w,
                      double h, int tile) {
  if (!gc || w <= 0 || h <= 0 || tile <= 0) return;
  gc->PushState();
  gc->Clip(x, y, w, h);
  gc->SetPen(*wxTRANSPARENT_PEN);
  gc->SetBrush(wxBrush(wxColour(32, 34, 38)));
  gc->DrawRectangle(x, y, w, h);
  gc->SetBrush(wxBrush(wxColour(44, 47, 52)));
  const int cols = static_cast<int>(std::ceil(w / tile));
  const int rows = static_cast<int>(std::ceil(h / tile));
  for (int r = 0; r < rows; ++r) {
    for (int c = (r & 1); c < cols; c += 2) {
      gc->DrawRectangle(x + c * tile, y + r * tile, tile, tile);
    }
  }
  gc->PopState();
}

wxGraphicsFont CachedGraphicsFont(wxGraphicsContext* gc, const wxFont& font,
                                    const wxColour& color) {
  if (!gc) return wxNullGraphicsFont;
  struct Entry {
    const wxGraphicsRenderer* renderer = nullptr;
    uint32_t rgba = 0;
    wxFont wx_font;
    wxGraphicsFont gfx_font;
  };
  static thread_local std::vector<Entry> cache;
  const wxGraphicsRenderer* renderer = gc->GetRenderer();
  const uint32_t rgba = PackRgba(color);
  for (const Entry& e : cache) {
    if (e.renderer == renderer && e.rgba == rgba && e.wx_font.IsSameAs(font)) {
      return e.gfx_font;
    }
  }
  wxGraphicsFont gfx = gc->CreateFont(font, color);
  if (cache.size() >= 64) cache.clear();
  cache.push_back(Entry{renderer, rgba, font, gfx});
  return gfx;
}

// ------------------------------------------------------------ OverlayCanvas ---

WxOverlayCanvas::WxOverlayCanvas(wxGraphicsContext* gc, const wxFont& font)
    : gc_(gc), font_(font) {}

WxOverlayCanvas::~WxOverlayCanvas() { FlushLines(); }

void WxOverlayCanvas::Flush() { FlushLines(); }

void WxOverlayCanvas::FlushLines() {
  if (!gc_ || !has_pending_lines_) return;
  wxPen pen(ToWx(pending_line_color_), pending_line_width_);
  if (pending_line_width_ > 1) pen.SetCap(wxCAP_ROUND);
  gc_->SetPen(pen);
  gc_->StrokePath(pending_line_path_);
  has_pending_lines_ = false;
  pending_line_path_ = wxGraphicsPath();
}

void WxOverlayCanvas::AddLine(glm::vec2 a, glm::vec2 b, OverlayColor color,
                              float thickness) {
  if (!gc_) return;
  const int width = std::max(1, static_cast<int>(std::round(thickness)));
  if (has_pending_lines_ &&
      (color != pending_line_color_ || width != pending_line_width_)) {
    FlushLines();
  }
  if (!has_pending_lines_) {
    pending_line_path_ = gc_->CreatePath();
    pending_line_color_ = color;
    pending_line_width_ = width;
    has_pending_lines_ = true;
  }
  pending_line_path_.MoveToPoint(a.x, a.y);
  pending_line_path_.AddLineToPoint(b.x, b.y);
}

void WxOverlayCanvas::AddCircle(glm::vec2 center, float radius,
                                OverlayColor color, float thickness) {
  if (!gc_) return;
  FlushLines();
  wxPen pen(ToWx(color), std::max(1, static_cast<int>(std::round(thickness))));
  gc_->SetPen(pen);
  gc_->SetBrush(*wxTRANSPARENT_BRUSH);
  gc_->DrawEllipse(center.x - radius, center.y - radius, radius * 2.0,
                   radius * 2.0);
}

void WxOverlayCanvas::AddCircleFilled(glm::vec2 center, float radius,
                                      OverlayColor color) {
  if (!gc_) return;
  FlushLines();
  gc_->SetPen(*wxTRANSPARENT_PEN);
  gc_->SetBrush(wxBrush(ToWx(color)));
  gc_->DrawEllipse(center.x - radius, center.y - radius, radius * 2.0,
                   radius * 2.0);
}

void WxOverlayCanvas::AddRect(glm::vec2 min, glm::vec2 max, OverlayColor color,
                              float rounding, float thickness) {
  if (!gc_) return;
  FlushLines();
  wxPen pen(ToWx(color), std::max(1, static_cast<int>(std::round(thickness))));
  gc_->SetPen(pen);
  gc_->SetBrush(*wxTRANSPARENT_BRUSH);
  if (rounding > 0.0f) {
    gc_->DrawRoundedRectangle(min.x, min.y, max.x - min.x, max.y - min.y,
                              rounding);
  } else {
    gc_->DrawRectangle(min.x, min.y, max.x - min.x, max.y - min.y);
  }
}

void WxOverlayCanvas::AddRectFilled(glm::vec2 min, glm::vec2 max,
                                    OverlayColor color, float rounding) {
  if (!gc_) return;
  FlushLines();
  gc_->SetPen(*wxTRANSPARENT_PEN);
  gc_->SetBrush(wxBrush(ToWx(color)));
  if (rounding > 0.0f) {
    gc_->DrawRoundedRectangle(min.x, min.y, max.x - min.x, max.y - min.y,
                              rounding);
  } else {
    gc_->DrawRectangle(min.x, min.y, max.x - min.x, max.y - min.y);
  }
}

void WxOverlayCanvas::AddQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                              glm::vec2 p3, OverlayColor color,
                              float thickness) {
  if (!gc_) return;
  FlushLines();
  wxGraphicsPath path = gc_->CreatePath();
  path.MoveToPoint(p0.x, p0.y);
  path.AddLineToPoint(p1.x, p1.y);
  path.AddLineToPoint(p2.x, p2.y);
  path.AddLineToPoint(p3.x, p3.y);
  path.CloseSubpath();
  wxPen pen(ToWx(color), std::max(1, static_cast<int>(std::round(thickness))));
  gc_->SetPen(pen);
  gc_->StrokePath(path);
}

void WxOverlayCanvas::AddQuadFilled(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                                    glm::vec2 p3, OverlayColor color) {
  if (!gc_) return;
  FlushLines();
  wxGraphicsPath path = gc_->CreatePath();
  path.MoveToPoint(p0.x, p0.y);
  path.AddLineToPoint(p1.x, p1.y);
  path.AddLineToPoint(p2.x, p2.y);
  path.AddLineToPoint(p3.x, p3.y);
  path.CloseSubpath();
  gc_->SetPen(*wxTRANSPARENT_PEN);
  gc_->SetBrush(wxBrush(ToWx(color)));
  gc_->FillPath(path);
}

void WxOverlayCanvas::AddTriangleFilled(glm::vec2 p0, glm::vec2 p1,
                                        glm::vec2 p2, OverlayColor color) {
  if (!gc_) return;
  FlushLines();
  wxGraphicsPath path = gc_->CreatePath();
  path.MoveToPoint(p0.x, p0.y);
  path.AddLineToPoint(p1.x, p1.y);
  path.AddLineToPoint(p2.x, p2.y);
  path.CloseSubpath();
  gc_->SetPen(*wxTRANSPARENT_PEN);
  gc_->SetBrush(wxBrush(ToWx(color)));
  gc_->FillPath(path);
}

void WxOverlayCanvas::AddText(glm::vec2 pos, OverlayColor color,
                              const std::string& text) {
  if (!gc_ || text.empty()) return;
  FlushLines();
  if (!has_active_font_ || PackRgba(color) != PackRgba(active_font_color_)) {
    gc_->SetFont(CachedGraphicsFont(gc_, font_, ToWx(color)));
    active_font_color_ = color;
    has_active_font_ = true;
  }
  gc_->DrawText(wxString::FromUTF8(text), pos.x, pos.y);
}

glm::vec2 WxOverlayCanvas::MeasureText(const std::string& text) const {
  if (!gc_ || text.empty()) return glm::vec2(0.0f);
  struct MeasureEntry {
    wxFont wx_font;
    std::string text;
    glm::vec2 extent;
  };
  static thread_local std::vector<MeasureEntry> cache;
  for (const MeasureEntry& e : cache) {
    if (e.text == text && e.wx_font.IsSameAs(font_)) {
      return e.extent;
    }
  }
  const wxColour text_col = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
  gc_->SetFont(CachedGraphicsFont(gc_, font_, text_col));
  has_active_font_ = false;
  double w = 0.0;
  double h = 0.0;
  gc_->GetTextExtent(wxString::FromUTF8(text), &w, &h);
  const glm::vec2 extent(static_cast<float>(w), static_cast<float>(h));
  if (cache.size() >= 256) cache.clear();
  cache.push_back(MeasureEntry{font_, text, extent});
  return extent;
}

// --------------------------------------------------------- CollapsibleSection

CollapsibleSection::CollapsibleSection(
    wxWindow* parent, const wxString& title, bool expanded,
    const wxString& badge,
    std::function<void(const wxPoint&)> on_context_menu,
    std::function<void(bool)> on_toggle)
    : wxPanel(parent, wxID_ANY),
      title_(title),
      badge_(badge),
      expanded_(expanded),
      on_context_(std::move(on_context_menu)),
      on_toggle_(std::move(on_toggle)) {
  auto* root = new wxBoxSizer(wxVERTICAL);

  header_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 24),
                        wxFULL_REPAINT_ON_RESIZE);
  header_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  header_->SetCursor(wxCursor(wxCURSOR_HAND));
  header_->Bind(wxEVT_PAINT, &CollapsibleSection::OnPaintHeader, this);
  header_->Bind(wxEVT_LEFT_DOWN, &CollapsibleSection::OnHeaderClick, this);
  header_->Bind(wxEVT_RIGHT_UP, &CollapsibleSection::OnHeaderRightClick, this);
  header_->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
    hovered_ = true;
    header_->Refresh();
  });
  header_->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
    hovered_ = false;
    header_->Refresh();
  });

  body_ = new wxPanel(this, wxID_ANY);
  body_sizer_ = new wxBoxSizer(wxVERTICAL);
  body_->SetSizer(body_sizer_);
  body_->Show(expanded_);

  root->Add(header_, 0, wxEXPAND);
  root->Add(body_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  SetSizer(root);
}

void CollapsibleSection::SetTitle(const wxString& title) {
  title_ = title;
  if (header_) header_->Refresh();
}

void CollapsibleSection::SetBadge(const wxString& badge) {
  badge_ = badge;
  if (header_) header_->Refresh();
}

void CollapsibleSection::SetExpanded(bool expanded, bool notify) {
  if (expanded_ == expanded) return;
  expanded_ = expanded;
  UpdateLayout();
  if (notify && on_toggle_) on_toggle_(expanded_);
}

void CollapsibleSection::FinishBuild() { UpdateLayout(); }

void CollapsibleSection::UpdateLayout() {
  if (body_) body_->Show(expanded_);
  if (header_) header_->Refresh();
  if (GetParent()) {
    Layout();
    GetParent()->InvalidateBestSize();
    if (auto* scrolled = wxDynamicCast(GetParent(), wxScrolledWindow)) {
      scrolled->FitInside();
    } else {
      GetParent()->Layout();
    }
  }
}

void CollapsibleSection::OnPaintHeader(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(header_);
  const wxSize sz = header_->GetClientSize();
  const wxColour bg = wxSystemSettings::GetColour(
      hovered_ ? wxSYS_COLOUR_BTNHIGHLIGHT : wxSYS_COLOUR_BTNFACE);
  const wxColour border = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
  const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
  const wxColour muted = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);

  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(bg));
  dc.DrawRectangle(0, 0, sz.x, sz.y);

  dc.SetPen(wxPen(border));
  dc.DrawLine(0, 0, sz.x, 0);
  dc.DrawLine(0, sz.y - 1, sz.x, sz.y - 1);

  // Disclosure triangle.
  dc.SetBrush(wxBrush(fg));
  dc.SetPen(*wxTRANSPARENT_PEN);
  wxPoint tri[3];
  if (expanded_) {
    tri[0] = wxPoint(8, 9);
    tri[1] = wxPoint(16, 9);
    tri[2] = wxPoint(12, 15);
  } else {
    tri[0] = wxPoint(10, 7);
    tri[1] = wxPoint(16, 12);
    tri[2] = wxPoint(10, 17);
  }
  dc.DrawPolygon(3, tri);

  dc.SetFont(HeaderFont());
  dc.SetTextForeground(fg);
  dc.DrawText(title_, 22, (sz.y - dc.GetCharHeight()) / 2);

  if (!badge_.IsEmpty()) {
    dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    dc.SetTextForeground(muted);
    const wxSize bsz = dc.GetTextExtent(badge_);
    dc.DrawText(badge_, sz.x - bsz.x - 8, (sz.y - bsz.y) / 2);
  }
}

void CollapsibleSection::OnHeaderClick(wxMouseEvent&) {
  SetExpanded(!expanded_, true);
}

void CollapsibleSection::OnHeaderRightClick(wxMouseEvent& evt) {
  if (on_context_) on_context_(header_->ClientToScreen(evt.GetPosition()));
}

// ------------------------------------------------------------- DragNumberCtrl

DragNumberCtrl::DragNumberCtrl(wxWindow* parent, const wxString& label,
                               const wxColour& tag_color, double value,
                               double step, bool integer_only, int precision)
    : wxPanel(parent, wxID_ANY),
      label_(label),
      tag_color_(tag_color),
      value_(value),
      step_(step),
      integer_only_(integer_only),
      precision_(precision) {
  auto* sizer = new wxBoxSizer(wxHORIZONTAL);

  if (!label_.IsEmpty()) {
    const int tag_w = std::max(18, static_cast<int>(label_.Length()) * 8 + 10);
    tag_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(tag_w, 22));
    tag_->SetBackgroundStyle(wxBG_STYLE_PAINT);
    tag_->SetCursor(wxCursor(wxCURSOR_SIZEWE));
    tag_->SetToolTip("Drag horizontally to adjust (Shift: fine, Ctrl: snap)");
    tag_->Bind(wxEVT_PAINT, &DragNumberCtrl::OnTagPaint, this);
    tag_->Bind(wxEVT_LEFT_DOWN, &DragNumberCtrl::OnTagMouse, this);
    tag_->Bind(wxEVT_LEFT_UP, &DragNumberCtrl::OnTagMouse, this);
    tag_->Bind(wxEVT_MOTION, &DragNumberCtrl::OnTagMouse, this);
    tag_->Bind(wxEVT_MOUSE_CAPTURE_LOST, &DragNumberCtrl::OnCaptureLost, this);
    sizer->Add(tag_, 0, wxEXPAND | wxRIGHT, 2);
  }

  text_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                         wxSize(56, -1), wxTE_PROCESS_ENTER);
  text_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { CommitText(); });
  text_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
    CommitText();
    e.Skip();
  });
  sizer->Add(text_, 1, wxEXPAND);

  SetSizer(sizer);
  SyncText();
}

void DragNumberCtrl::SetValue(double v) {
  value_ = integer_only_ ? std::round(v) : v;
  if (!IsEditingText()) SyncText();
}

bool DragNumberCtrl::IsEditingText() const {
  return text_ && text_->HasFocus();
}

void DragNumberCtrl::SetCallbacks(std::function<void()> on_begin,
                                  std::function<void(double)> on_change,
                                  std::function<void()> on_end) {
  on_begin_ = std::move(on_begin);
  on_change_ = std::move(on_change);
  on_end_ = std::move(on_end);
}

void DragNumberCtrl::SyncText() {
  if (!text_) return;
  wxString s;
  if (integer_only_) {
    s = wxString::Format("%lld", static_cast<long long>(std::llround(value_)));
  } else {
    s = wxString::Format(wxString::Format("%%.%df", precision_), value_);
    // Trim trailing zeroes lightly while keeping clean precision.
    if (s.Contains('.')) {
      while (s.EndsWith('0')) s.RemoveLast();
      if (s.EndsWith('.')) s.RemoveLast();
    }
  }
  if (text_->GetValue() != s) text_->ChangeValue(s);
}

void DragNumberCtrl::CommitText() {
  if (!text_) return;
  double parsed = value_;
  if (text_->GetValue().ToCDouble(&parsed)) {
    if (integer_only_) parsed = std::round(parsed);
    if (parsed != value_) {
      value_ = parsed;
      if (on_begin_) on_begin_();
      if (on_change_) on_change_(value_);
      if (on_end_) on_end_();
    }
  }
  SyncText();
}

void DragNumberCtrl::OnTagPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(tag_);
  const wxSize sz = tag_->GetClientSize();
  const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
  const wxColour border = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
  const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);

  dc.SetPen(wxPen(border));
  dc.SetBrush(wxBrush(bg));
  dc.DrawRectangle(0, 0, sz.x, sz.y);

  if (tag_color_.IsOk()) {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(tag_color_));
    dc.DrawRectangle(1, 1, 3, std::max(0, sz.y - 2));
  }

  dc.SetFont(HeaderFont(-1));
  dc.SetTextForeground(fg);
  const wxSize tsz = dc.GetTextExtent(label_);
  dc.DrawText(label_, std::max(5, (sz.x - tsz.x) / 2), (sz.y - tsz.y) / 2);
}

void DragNumberCtrl::OnTagMouse(wxMouseEvent& evt) {
  if (evt.LeftDown()) {
    dragging_ = true;
    drag_start_x_ = evt.GetX();
    drag_start_val_ = value_;
    if (!tag_->HasCapture()) tag_->CaptureMouse();
    if (on_begin_) on_begin_();
  } else if (evt.LeftUp() && dragging_) {
    dragging_ = false;
    if (tag_->HasCapture()) tag_->ReleaseMouse();
    if (on_end_) on_end_();
  } else if (evt.Dragging() && dragging_) {
    const double mult = evt.ShiftDown() ? 0.1 : (evt.ControlDown() ? 5.0 : 1.0);
    double next = drag_start_val_ + (evt.GetX() - drag_start_x_) * step_ * mult;
    if (integer_only_) next = std::round(next);
    if (next != value_) {
      value_ = next;
      SyncText();
      if (on_change_) on_change_(value_);
    }
  }
}

void DragNumberCtrl::OnCaptureLost(wxMouseCaptureLostEvent&) {
  if (dragging_) {
    dragging_ = false;
    if (on_end_) on_end_();
  }
}

// ----------------------------------------------------------------- Vec2Field

Vec2Field::Vec2Field(wxWindow* parent, glm::vec2 value, double step)
    : wxPanel(parent, wxID_ANY) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  x_ = new DragNumberCtrl(this, "X", Colors::kAxisX, value.x, step);
  y_ = new DragNumberCtrl(this, "Y", Colors::kAxisY, value.y, step);
  row->Add(x_, 1, wxEXPAND | wxRIGHT, 4);
  row->Add(y_, 1, wxEXPAND);
  SetSizer(row);
}

void Vec2Field::SetValue(glm::vec2 v) {
  x_->SetValue(v.x);
  y_->SetValue(v.y);
}

glm::vec2 Vec2Field::GetValue() const {
  return {static_cast<float>(x_->GetValue()), static_cast<float>(y_->GetValue())};
}

void Vec2Field::SetCallbacks(std::function<void()> on_begin,
                             std::function<void(glm::vec2)> on_change,
                             std::function<void()> on_end) {
  auto notify = [this, on_change](double) {
    if (on_change) on_change(GetValue());
  };
  x_->SetCallbacks(on_begin, notify, on_end);
  y_->SetCallbacks(on_begin, notify, on_end);
}

bool Vec2Field::IsEditingText() const {
  return x_->IsEditingText() || y_->IsEditingText();
}

// ----------------------------------------------------------------- Vec3Field

Vec3Field::Vec3Field(wxWindow* parent, glm::vec3 value, double step)
    : wxPanel(parent, wxID_ANY) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  x_ = new DragNumberCtrl(this, "X", Colors::kAxisX, value.x, step);
  y_ = new DragNumberCtrl(this, "Y", Colors::kAxisY, value.y, step);
  z_ = new DragNumberCtrl(this, "Z", Colors::kAxisZ, value.z, step);
  row->Add(x_, 1, wxEXPAND | wxRIGHT, 4);
  row->Add(y_, 1, wxEXPAND | wxRIGHT, 4);
  row->Add(z_, 1, wxEXPAND);
  SetSizer(row);
}

void Vec3Field::SetValue(glm::vec3 v) {
  x_->SetValue(v.x);
  y_->SetValue(v.y);
  z_->SetValue(v.z);
}

glm::vec3 Vec3Field::GetValue() const {
  return {static_cast<float>(x_->GetValue()), static_cast<float>(y_->GetValue()),
          static_cast<float>(z_->GetValue())};
}

void Vec3Field::SetCallbacks(std::function<void()> on_begin,
                             std::function<void(glm::vec3)> on_change,
                             std::function<void()> on_end) {
  auto notify = [this, on_change](double) {
    if (on_change) on_change(GetValue());
  };
  x_->SetCallbacks(on_begin, notify, on_end);
  y_->SetCallbacks(on_begin, notify, on_end);
  z_->SetCallbacks(on_begin, notify, on_end);
}

bool Vec3Field::IsEditingText() const {
  return x_->IsEditingText() || y_->IsEditingText() || z_->IsEditingText();
}

// ----------------------------------------------------------- ColorSwatchCtrl

ColorSwatchCtrl::ColorSwatchCtrl(wxWindow* parent, glm::vec4 rgba)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(90, 22)),
      rgba_(rgba) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetCursor(wxCursor(wxCURSOR_HAND));
  SetToolTip("Click to choose color");
  Bind(wxEVT_PAINT, &ColorSwatchCtrl::OnPaint, this);
  Bind(wxEVT_LEFT_UP, &ColorSwatchCtrl::OnClick, this);
}

void ColorSwatchCtrl::SetValue(glm::vec4 rgba) {
  rgba_ = rgba;
  Refresh();
}

void ColorSwatchCtrl::SetOnChange(std::function<void(glm::vec4)> on_change) {
  on_change_ = std::move(on_change);
}

void ColorSwatchCtrl::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(this);
  const wxSize sz = GetClientSize();
  auto to_u8 = [](float f) -> unsigned char {
    return static_cast<unsigned char>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
  dc.SetBrush(wxBrush(wxColour(to_u8(rgba_.r), to_u8(rgba_.g), to_u8(rgba_.b))));
  dc.DrawRectangle(0, 0, sz.x, sz.y);

  // Alpha bottom bar.
  const int alpha_w = static_cast<int>(std::clamp(rgba_.a, 0.0f, 1.0f) * (sz.x - 2));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(*wxWHITE));
  dc.DrawRectangle(1, sz.y - 4, alpha_w, 3);
}

void ColorSwatchCtrl::OnClick(wxMouseEvent&) {
  auto to_u8 = [](float f) -> unsigned char {
    return static_cast<unsigned char>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  wxColourData data;
  data.SetChooseFull(true);
  data.SetChooseAlpha(true);
  data.SetColour(wxColour(to_u8(rgba_.r), to_u8(rgba_.g), to_u8(rgba_.b),
                          to_u8(rgba_.a)));
  wxColourDialog dlg(this, &data);
  if (dlg.ShowModal() == wxID_OK) {
    const wxColour c = dlg.GetColourData().GetColour();
    rgba_ = glm::vec4(c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f,
                      c.Alpha() / 255.0f);
    Refresh();
    if (on_change_) on_change_(rgba_);
  }
}

}  // namespace Mercury::EditorUI
