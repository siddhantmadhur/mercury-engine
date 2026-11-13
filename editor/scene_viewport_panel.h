/*
 * scene_viewport_panel.h
 *
 * Dockable Scene Viewport displaying Vulkan offscreen pixels, 2D/3D gizmo
 * overlays via WxOverlayCanvas, HUD toolbar, WASDQE/orbit/zoom camera controls,
 * asset/OS drag-and-drop, embedded Camera Preview pip, and detachable
 * CameraPreviewFrame window.
 */
#ifndef MERCURY_EDITOR_SCENE_VIEWPORT_PANEL_H
#define MERCURY_EDITOR_SCENE_VIEWPORT_PANEL_H

#include <wx/tglbtn.h>
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editor_scene.h"

namespace Mercury::EditorUI {

class CameraPreviewFrame : public wxFrame {
 public:
  CameraPreviewFrame(wxWindow* parent, EditorScene& scene,
                     std::function<void()> on_closed);

  void SyncFromController();

 private:
  void OnPaint(wxPaintEvent& evt);
  void OnClose(wxCloseEvent& evt);

  EditorScene& scene_;
  std::function<void()> on_closed_;
  wxPanel* canvas_ = nullptr;
  std::vector<uint8_t> pixels_;
  wxBitmap bitmap_;
  uint64_t last_frame_id_ = 0;
  std::string last_label_;
};

class SceneViewportPanel : public wxPanel {
 public:
  SceneViewportPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController(float dt);
  void SetPreviewPoppedOut(bool popped_out);
  bool IsPreviewPoppedOut() const { return preview_popped_out_; }
  void SetOnPopoutPreview(std::function<void(bool)> cb) {
    on_popout_preview_ = std::move(cb);
  }

 private:
  void SyncToolbarState();
  void OnPaintCanvas(wxPaintEvent& evt);
  void OnCanvasMouseDown(wxMouseEvent& evt);
  void OnCanvasMouseUp(wxMouseEvent& evt);
  void OnCanvasMouseMove(wxMouseEvent& evt);
  void OnCanvasWheel(wxMouseEvent& evt);
  void OnCanvasKeyDown(wxKeyEvent& evt);
  void ReleaseMouseIfCaptured();
  bool HandleDroppedResPath(const std::string& res_path);

  EditorScene& scene_;

  wxToggleButton* move_btn_ = nullptr;
  wxToggleButton* rotate_btn_ = nullptr;
  wxToggleButton* scale_btn_ = nullptr;
  wxToggleButton* space_btn_ = nullptr;
  wxToggleButton* mode_2d_btn_ = nullptr;
  wxButton* frame_btn_ = nullptr;
  wxStaticText* status_lbl_ = nullptr;
  wxPanel* canvas_ = nullptr;

  wxFont overlay_font_;
  std::vector<uint8_t> scene_pixels_;
  wxBitmap scene_bmp_;
  uint64_t scene_frame_id_ = 0;

  std::vector<uint8_t> preview_pixels_;
  wxBitmap preview_bmp_;
  uint64_t preview_frame_id_ = 0;
  EditorScene::CameraPreviewInfo last_preview_info_;
  wxRect pip_popout_rect_;

  bool preview_popped_out_ = false;
  std::function<void(bool)> on_popout_preview_;

  bool hovered_ = false;
  glm::vec2 local_mouse_{0.0f, 0.0f};
  glm::vec2 prev_mouse_{0.0f, 0.0f};
  bool mouse_pos_valid_ = false;
  bool left_down_ = false;
  bool right_down_ = false;
  bool middle_down_ = false;

  EditorScene::Tool last_tool_ = EditorScene::Tool::kMove;
  EditorScene::HandleSpace last_space_ = EditorScene::HandleSpace::kLocal;
  bool last_2d_ = true;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCENE_VIEWPORT_PANEL_H
