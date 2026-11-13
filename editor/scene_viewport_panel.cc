#include "scene_viewport_panel.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "scene/scene.h"
#include "dnd_payloads.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

std::string LowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return e;
}

bool IsModelExt(const std::string& ext) {
  return ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" ||
         ext == ".dae" || ext == ".ply";
}

}  // namespace

// ------------------------------------------------------ CameraPreviewFrame ---

CameraPreviewFrame::CameraPreviewFrame(wxWindow* parent, EditorScene& scene,
                                       std::function<void()> on_closed)
    : wxFrame(parent, wxID_ANY, "Camera Preview", wxDefaultPosition,
              wxSize(420, 260),
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
      scene_(scene),
      on_closed_(std::move(on_closed)) {
  ApplyDarkPalette(this);
  canvas_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
  canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  canvas_->SetBackgroundColour(Colors::kBgCanvas);

  auto* sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(canvas_, 1, wxEXPAND);
  SetSizer(sizer);

  canvas_->Bind(wxEVT_PAINT, &CameraPreviewFrame::OnPaint, this);
  Bind(wxEVT_CLOSE_WINDOW, &CameraPreviewFrame::OnClose, this);
}

void CameraPreviewFrame::SyncFromController() {
  if (!IsShown()) return;
  const wxSize sz = canvas_->GetClientSize();
  if (sz.x < 4 || sz.y < 4) return;
  auto info = scene_.UpdatePreviewCamera(sz.x, sz.y);
  if (info.label != last_label_) {
    last_label_ = info.label;
    SetTitle(info.label.empty()
                 ? wxString("Camera Preview")
                 : wxString::FromUTF8("Camera Preview — " + info.label));
  }
  canvas_->Refresh(false);
}

void CameraPreviewFrame::OnPaint(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(canvas_);
  const wxSize sz = canvas_->GetClientSize();
  dc.SetBrush(wxBrush(Colors::kBgCanvas));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(0, 0, sz.x, sz.y);

  RenderHost* host = scene_.GetRenderHost();
  if (!host) return;
  int w = 0;
  int h = 0;
  uint64_t fid = 0;
  if (host->CopyPreviewPixels(w, h, pixels_, &fid) && w > 0 && h > 0) {
    if (!bitmap_.IsOk() || fid != last_frame_id_ || bitmap_.GetWidth() != w ||
        bitmap_.GetHeight() != h) {
      last_frame_id_ = fid;
      bitmap_ = RgbaToWxBitmap(pixels_.data(), w, h, false);
    }
  }
  if (bitmap_.IsOk()) {
    const double sx = static_cast<double>(sz.x) / bitmap_.GetWidth();
    const double sy = static_cast<double>(sz.y) / bitmap_.GetHeight();
    const double s = std::min(sx, sy);
    const int dw = std::max(1, static_cast<int>(bitmap_.GetWidth() * s));
    const int dh = std::max(1, static_cast<int>(bitmap_.GetHeight() * s));
    const int ox = (sz.x - dw) / 2;
    const int oy = (sz.y - dh) / 2;
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (gc) {
      gc->DrawBitmap(bitmap_, ox, oy, dw, dh);
      return;
    }
    dc.DrawBitmap(bitmap_, ox, oy, false);
  } else {
    dc.SetTextForeground(Colors::kTextMuted);
    dc.DrawText("No camera preview available", 14, 14);
  }
}

void CameraPreviewFrame::OnClose(wxCloseEvent& evt) {
  Hide();
  if (on_closed_) on_closed_();
  evt.Veto();
}

// ------------------------------------------------------ SceneViewportPanel ---

SceneViewportPanel::SceneViewportPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY),
      scene_(scene),
      overlay_font_(wxFontInfo(9).Family(wxFONTFAMILY_SWISS)) {
  ApplyDarkPalette(this);
  auto* root = new wxBoxSizer(wxVERTICAL);

  // Dark viewport HUD toolbar
  auto* bar = new wxPanel(this, wxID_ANY);
  bar->SetBackgroundColour(Colors::kBgHeader);
  auto* bs = new wxBoxSizer(wxHORIZONTAL);

  auto make_btn = [&](const wxString& label, const wxString& tip) {
    auto* b = new wxButton(bar, wxID_ANY, label, wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
    b->SetToolTip(tip);
    StyleButton(b);
    return b;
  };

  move_btn_ = make_btn("Move", "Translate gizmo (W)");
  rotate_btn_ = make_btn("Rotate", "Rotate gizmo (E)");
  scale_btn_ = make_btn("Scale", "Scale gizmo (R)");
  space_btn_ = make_btn("Local", "Toggle Local / Global handle space (X)");
  mode_2d_btn_ = make_btn("2D", "Toggle 2D / 3D scene camera");
  frame_btn_ = make_btn("Frame", "Frame selected node or map (F)");

  bs->Add(move_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(rotate_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(scale_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  bs->Add(space_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(mode_2d_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(frame_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

  status_lbl_ = new wxStaticText(bar, wxID_ANY, wxEmptyString);
  status_lbl_->SetForegroundColour(Colors::kAccentAmber);
  bs->Add(status_lbl_, 1, wxALIGN_CENTER_VERTICAL);

  auto* bar_wrap = new wxBoxSizer(wxVERTICAL);
  bar_wrap->Add(bs, 0, wxEXPAND | wxALL, 4);
  bar->SetSizer(bar_wrap);
  root->Add(bar, 0, wxEXPAND);

  canvas_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                        wxBORDER_NONE | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE);
  canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  canvas_->SetBackgroundColour(Colors::kBgCanvas);
  root->Add(canvas_, 1, wxEXPAND);
  SetSizer(root);

  move_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.SetTool(EditorScene::Tool::kMove);
    SyncToolbarState();
  });
  rotate_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.SetTool(EditorScene::Tool::kRotate);
    SyncToolbarState();
  });
  scale_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.SetTool(EditorScene::Tool::kScale);
    SyncToolbarState();
  });
  space_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.ToggleHandleSpace();
    SyncToolbarState();
  });
  mode_2d_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.ToggleView2D();
    SyncToolbarState();
  });
  frame_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (scene_.GetSelectedNode()) scene_.FrameSelected();
    else scene_.FrameMap();
  });

  canvas_->Bind(wxEVT_PAINT, &SceneViewportPanel::OnPaintCanvas, this);
  canvas_->Bind(wxEVT_LEFT_DOWN, &SceneViewportPanel::OnCanvasMouseDown, this);
  canvas_->Bind(wxEVT_RIGHT_DOWN, &SceneViewportPanel::OnCanvasMouseDown, this);
  canvas_->Bind(wxEVT_MIDDLE_DOWN, &SceneViewportPanel::OnCanvasMouseDown, this);
  canvas_->Bind(wxEVT_LEFT_UP, &SceneViewportPanel::OnCanvasMouseUp, this);
  canvas_->Bind(wxEVT_RIGHT_UP, &SceneViewportPanel::OnCanvasMouseUp, this);
  canvas_->Bind(wxEVT_MIDDLE_UP, &SceneViewportPanel::OnCanvasMouseUp, this);
  canvas_->Bind(wxEVT_MOTION, &SceneViewportPanel::OnCanvasMouseMove, this);
  canvas_->Bind(wxEVT_MOUSEWHEEL, &SceneViewportPanel::OnCanvasWheel, this);
  canvas_->Bind(wxEVT_KEY_DOWN, &SceneViewportPanel::OnCanvasKeyDown, this);
  canvas_->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hovered_ = true; });
  canvas_->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hovered_ = false; });
  canvas_->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
    left_down_ = right_down_ = middle_down_ = false;
    scene_.OnViewportMouseUp();
  });

  canvas_->SetDropTarget(new CompositeDropTarget(
      [this](wxCoord, wxCoord, const wxArrayString& files) -> bool {
        auto proj = scene_.GetProject();
        if (!proj) return false;
        const fs::path root = fs::weakly_canonical(proj->GetRoot());
        for (const wxString& wf : files) {
          fs::path src(wf.ToStdString());
          std::error_code ec;
          fs::path canonical_src = fs::weakly_canonical(src, ec);
          if (ec) continue;
          fs::path rel = canonical_src.lexically_relative(root);
          std::string res_path;
          if (!rel.empty() && rel.string().rfind("..", 0) != 0) {
            res_path = rel.generic_string();
          } else {
            // Copy dropped external asset into assets/
            fs::path dest_dir = root / "assets";
            fs::create_directories(dest_dir, ec);
            fs::path dest = dest_dir / src.filename();
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
              res_path =
                  dest.lexically_relative(root).generic_string();
            }
          }
          if (!res_path.empty() && HandleDroppedResPath(res_path)) return true;
        }
        return false;
      },
      [this](wxCoord, wxCoord, const wxString& text) -> bool {
        if (auto res = ExtractResPathPayload(text)) {
          return HandleDroppedResPath(*res);
        }
        return false;
      }));

  SyncToolbarState();
}

void SceneViewportPanel::SetPreviewPoppedOut(bool popped_out) {
  preview_popped_out_ = popped_out;
  canvas_->Refresh(false);
}

bool SceneViewportPanel::HandleDroppedResPath(const std::string& res_path) {
  const std::string ext = LowerExt(res_path);
  if (ext == ".mmap") {
    scene_.OpenMap(res_path);
    return true;
  }
  if (IsModelExt(ext) && !scene_.IsModelPreloading()) {
    scene_.ImportModel(res_path);
    return true;
  }
  return false;
}

void SceneViewportPanel::SyncToolbarState() {
  const auto tool = scene_.GetTool();
  const auto space = scene_.GetHandleSpace();
  const bool view_2d = scene_.IsView2D();

  StyleButton(move_btn_, tool == EditorScene::Tool::kMove, Colors::kAccentBlue);
  StyleButton(rotate_btn_, tool == EditorScene::Tool::kRotate,
              Colors::kAccentBlue);
  StyleButton(scale_btn_, tool == EditorScene::Tool::kScale, Colors::kAccentBlue);
  StyleButton(space_btn_, space == EditorScene::HandleSpace::kGlobal,
              Colors::kAccentAmber);
  space_btn_->SetLabel(space == EditorScene::HandleSpace::kGlobal ? "Global"
                                                                  : "Local");
  StyleButton(mode_2d_btn_, view_2d, Colors::kAccentBlue);
  mode_2d_btn_->SetLabel(view_2d ? "2D" : "3D");

  last_tool_ = tool;
  last_space_ = space;
  last_2d_ = view_2d;
  Layout();
}

void SceneViewportPanel::SyncFromController(float dt) {
  const wxSize sz = canvas_->GetClientSize();
  if (sz.x > 0 && sz.y > 0) {
    scene_.SetSceneViewportSize(
        {static_cast<float>(sz.x), static_cast<float>(sz.y)});
  }

  if (scene_.GetTool() != last_tool_ || scene_.GetHandleSpace() != last_space_ ||
      scene_.IsView2D() != last_2d_) {
    SyncToolbarState();
  }

  if (scene_.IsModelPreloading()) {
    status_lbl_->SetLabel(wxString::FromUTF8(
        "Loading " + fs::path(scene_.PendingImportResPath()).filename().string() +
        "..."));
  } else {
    status_lbl_->SetLabel(wxEmptyString);
  }

  // Gather WASDQE camera fly keys when canvas is focused & right-mouse held
  int wasdqe = 0;
  if (right_down_ && canvas_->HasFocus()) {
    if (wxGetKeyState(static_cast<wxKeyCode>('W'))) wasdqe |= EditorScene::kFlyW;
    if (wxGetKeyState(static_cast<wxKeyCode>('S'))) wasdqe |= EditorScene::kFlyS;
    if (wxGetKeyState(static_cast<wxKeyCode>('D'))) wasdqe |= EditorScene::kFlyD;
    if (wxGetKeyState(static_cast<wxKeyCode>('A'))) wasdqe |= EditorScene::kFlyA;
    if (wxGetKeyState(static_cast<wxKeyCode>('E'))) wasdqe |= EditorScene::kFlyE;
    if (wxGetKeyState(static_cast<wxKeyCode>('Q'))) wasdqe |= EditorScene::kFlyQ;
  }

  glm::vec2 delta(0.0f, 0.0f);
  if (mouse_pos_valid_) {
    delta = local_mouse_ - prev_mouse_;
    prev_mouse_ = local_mouse_;
  }
  const bool shift_down = wxGetKeyState(WXK_SHIFT);
  scene_.UpdateViewportNavigation(delta, left_down_, right_down_, middle_down_,
                                  wasdqe, shift_down, dt);

  const bool alt_down = wxGetKeyState(WXK_ALT);
  scene_.UpdateHoveredHandle(local_mouse_, hovered_, alt_down, right_down_);

  if (!preview_popped_out_) {
    last_preview_info_ = scene_.UpdatePreviewCamera();
  } else {
    last_preview_info_ = {};
  }

  canvas_->Refresh(false);
}

void SceneViewportPanel::OnPaintCanvas(wxPaintEvent&) {
  wxAutoBufferedPaintDC dc(canvas_);
  const wxSize sz = canvas_->GetClientSize();
  dc.SetBrush(wxBrush(Colors::kBgCanvas));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(0, 0, sz.x, sz.y);

  RenderHost* host = scene_.GetRenderHost();
  if (host) {
    int w = 0;
    int h = 0;
    uint64_t fid = 0;
    if (host->CopyScenePixels(w, h, scene_pixels_, &fid) && w > 0 && h > 0) {
      if (!scene_bmp_.IsOk() || fid != scene_frame_id_ ||
          scene_bmp_.GetWidth() != w || scene_bmp_.GetHeight() != h) {
        scene_frame_id_ = fid;
        scene_bmp_ = RgbaToWxBitmap(scene_pixels_.data(), w, h, false);
      }
    }
  }

  std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
  if (!gc) return;

  if (scene_bmp_.IsOk()) {
    gc->DrawBitmap(scene_bmp_, 0, 0, sz.x, sz.y);
  }

  WxOverlayCanvas overlay(gc.get(), overlay_font_);
  const bool alt_down = wxGetKeyState(WXK_ALT);
  scene_.RenderOverlays(overlay, local_mouse_, hovered_, alt_down, right_down_);

  // Bottom-left navigation hint pill
  if (const char* hint = scene_.GetHintText(); hint && *hint) {
    const glm::vec2 text_sz = overlay.MeasureText(hint);
    const glm::vec2 pad(8.0f, 4.0f);
    const glm::vec2 min_p(10.0f, sz.y - text_sz.y - pad.y * 2 - 10.0f);
    const glm::vec2 max_p(min_p.x + text_sz.x + pad.x * 2,
                          min_p.y + text_sz.y + pad.y * 2);
    overlay.AddRectFilled(min_p, max_p, OverlayColor{20, 22, 26, 200}, 4.0f);
    overlay.AddText(min_p + pad, OverlayColor{215, 220, 228, 255}, hint);
  }

  // Embedded Camera Preview picture-in-picture when not popped out
  pip_popout_rect_ = wxRect();
  if (!preview_popped_out_ && last_preview_info_.entity != entt::null &&
      last_preview_info_.width > 0 && last_preview_info_.height > 0 && host) {
    int pw = 0;
    int ph = 0;
    uint64_t pfid = 0;
    if (host->CopyPreviewPixels(pw, ph, preview_pixels_, &pfid) && pw > 0 &&
        ph > 0) {
      if (!preview_bmp_.IsOk() || pfid != preview_frame_id_ ||
          preview_bmp_.GetWidth() != pw || preview_bmp_.GetHeight() != ph) {
        preview_frame_id_ = pfid;
        preview_bmp_ = RgbaToWxBitmap(preview_pixels_.data(), pw, ph, false);
      }
    }

    const int margin = 12;
    const int header_h = 22;
    const int pip_w = std::min(last_preview_info_.width, std::max(120, sz.x - 40));
    const int pip_h = std::max(60, pip_w * last_preview_info_.height /
                                       std::max(1, last_preview_info_.width));
    const int x = sz.x - pip_w - margin;
    const int y = sz.y - pip_h - header_h - margin;
    if (x >= 8 && y >= 8) {
      gc->SetBrush(wxBrush(wxColour(24, 26, 30, 235)));
      gc->SetPen(wxPen(Colors::kBorderStrong, 1));
      gc->DrawRoundedRectangle(x, y, pip_w, pip_h + header_h, 4.0);

      gc->SetFont(overlay_font_, Colors::kTextPrimary);
      gc->DrawText(wxString::FromUTF8(last_preview_info_.label), x + 8, y + 3);

      const wxString pop_lbl = "Pop Out";
      double tw = 0.0;
      double th = 0.0;
      gc->GetTextExtent(pop_lbl, &tw, &th);
      const int bx = x + pip_w - static_cast<int>(tw) - 12;
      const int by = y + 2;
      const int bw = static_cast<int>(tw) + 8;
      const int bh = header_h - 4;
      pip_popout_rect_ = wxRect(bx, by, bw, bh);
      gc->SetBrush(wxBrush(Colors::kBgControl));
      gc->SetPen(wxPen(Colors::kBorderStrong, 1));
      gc->DrawRoundedRectangle(bx, by, bw, bh, 3.0);
      gc->SetFont(overlay_font_, Colors::kTextPrimary);
      gc->DrawText(pop_lbl, bx + 4, by + 1);

      if (preview_bmp_.IsOk()) {
        gc->DrawBitmap(preview_bmp_, x + 1, y + header_h, pip_w - 2, pip_h - 1);
      }
    }
  }
}

void SceneViewportPanel::ReleaseMouseIfCaptured() {
  if (canvas_->HasCapture()) canvas_->ReleaseMouse();
}

void SceneViewportPanel::OnCanvasMouseDown(wxMouseEvent& evt) {
  canvas_->SetFocus();
  const wxPoint p = evt.GetPosition();
  local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
  prev_mouse_ = local_mouse_;
  mouse_pos_valid_ = true;

  if (evt.LeftDown()) {
    if (!pip_popout_rect_.IsEmpty() && pip_popout_rect_.Contains(p)) {
      if (on_popout_preview_) on_popout_preview_(true);
      return;
    }
    left_down_ = true;
    if (!canvas_->HasCapture()) canvas_->CaptureMouse();
    if (!evt.AltDown() && !right_down_ && !middle_down_) {
      scene_.OnViewportMouseDown(local_mouse_, 0, evt.AltDown());
    }
  } else if (evt.RightDown()) {
    right_down_ = true;
    if (!canvas_->HasCapture()) canvas_->CaptureMouse();
  } else if (evt.MiddleDown()) {
    middle_down_ = true;
    if (!canvas_->HasCapture()) canvas_->CaptureMouse();
  }
}

void SceneViewportPanel::OnCanvasMouseUp(wxMouseEvent& evt) {
  if (evt.LeftUp()) {
    left_down_ = false;
    scene_.OnViewportMouseUp();
  } else if (evt.RightUp()) {
    right_down_ = false;
  } else if (evt.MiddleUp()) {
    middle_down_ = false;
  }
  if (!left_down_ && !right_down_ && !middle_down_) {
    ReleaseMouseIfCaptured();
  }
}

void SceneViewportPanel::OnCanvasMouseMove(wxMouseEvent& evt) {
  const wxPoint p = evt.GetPosition();
  local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
  if (!mouse_pos_valid_) {
    prev_mouse_ = local_mouse_;
    mouse_pos_valid_ = true;
  }
  hovered_ = true;

  if (left_down_ && scene_.IsDragging()) {
    scene_.OnViewportMouseDrag(local_mouse_, evt.ControlDown());
  }
}

void SceneViewportPanel::OnCanvasWheel(wxMouseEvent& evt) {
  const wxPoint p = evt.GetPosition();
  local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
  const int delta = evt.GetWheelDelta();
  const float steps =
      delta > 0 ? static_cast<float>(evt.GetWheelRotation()) / delta : 0.0f;
  if (steps != 0.0f) {
    scene_.OnViewportWheel(steps, local_mouse_);
  }
}

void SceneViewportPanel::OnCanvasKeyDown(wxKeyEvent& evt) {
  if (right_down_ || evt.ControlDown() || evt.AltDown()) {
    evt.Skip();
    return;
  }
  switch (evt.GetKeyCode()) {
    case 'W':
    case 'w':
      scene_.SetTool(EditorScene::Tool::kMove);
      SyncToolbarState();
      return;
    case 'E':
    case 'e':
      scene_.SetTool(EditorScene::Tool::kRotate);
      SyncToolbarState();
      return;
    case 'R':
    case 'r':
      scene_.SetTool(EditorScene::Tool::kScale);
      SyncToolbarState();
      return;
    case 'X':
    case 'x':
      scene_.ToggleHandleSpace();
      SyncToolbarState();
      return;
    case 'F':
    case 'f':
      if (scene_.GetSelectedNode()) scene_.FrameSelected();
      else scene_.FrameMap();
      return;
    default:
      evt.Skip();
      return;
  }
}

}  // namespace Mercury::EditorUI
