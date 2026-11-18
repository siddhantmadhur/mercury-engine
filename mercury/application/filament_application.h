// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURYENGINE_FILAMENT_APPLICATION_H
#define MERCURYENGINE_FILAMENT_APPLICATION_H

#include <filament/Camera.h>
#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <utils/Entity.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "application.h"
#include "filament/filament_renderer.h"
#include "filament/filament_ui.h"

namespace Mercury {

/**
 * @brief Mercury's renderer: Filament (google/filament) on Vulkan
 *
 * The scene's RenderMesh entities, materials, textures and lights are
 * mirrored into Filament every frame; each output (the window, the
 * editor's offscreen viewports and the UI Builder canvas) is a View, so
 * Filament's PBR shading, shadowing, ambient occlusion, bloom and
 * post-processing all come from the library.
 */
class FilamentApplication : public Application {
 public:
  FilamentApplication(const std::string& window_title,
                      float initial_width = 1280, float initial_height = 720);
  ~FilamentApplication() override;

  // RenderHost (the editor's offscreen viewports).
  void EnableSceneTexture(bool enabled) override;
  void SetSceneViewportSize(int width, int height) override;
  bool CopyScenePixels(int& width, int& height, std::vector<uint8_t>& rgba_out,
                       uint64_t* frame_id = nullptr) override;
  void SetPreviewCamera(kEntityId camera, int width, int height) override;
  bool CopyPreviewPixels(int& width, int& height,
                         std::vector<uint8_t>& rgba_out,
                         uint64_t* frame_id = nullptr) override;
  void SetUIPreview(const UI::UIDrawData* draw_data, int width, int height,
                    glm::vec4 background) override;
  bool CopyUIPreviewPixels(int& width, int& height,
                           std::vector<uint8_t>& rgba_out,
                           uint64_t* frame_id = nullptr) override;
  void SetShowRenderStats(bool show) override { show_stats_ = show; }

 protected:
  bool OnLoad() override;
  void Render() override;
  bool Initialize() override;
  void OnNewTick() override {}
  void OnResize(int32_t width, int32_t height) override;
  void RequestCapture(const std::string& file) override;

 private:
  // An offscreen output: a RenderTarget with its color/depth textures, the
  // View rendering into it, and the last frame's read-back pixels.
  struct Offscreen {
    filament::RenderTarget* target = nullptr;
    filament::Texture* color = nullptr;
    filament::Texture* depth = nullptr;
    filament::View* view = nullptr;
    filament::Camera* camera = nullptr;
    utils::Entity camera_entity;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;  // RGBA, top row first
    bool has_pixels = false;
    uint64_t frame_id = 0;
  };

  // (Re)creates the offscreen target at `width` x `height` when it changed.
  void EnsureOffscreen(Offscreen& target, uint32_t width, uint32_t height,
                       filament::Scene* scene, bool is_ui);
  void DestroyOffscreen(Offscreen& target);
  // Copies the latest frame out of `target` (top-down RGBA) if there is one.
  bool CopyOffscreen(Offscreen& target, int& width, int& height,
                     std::vector<uint8_t>& rgba_out, uint64_t* frame_id);
  // Points `camera` at what `render` sees, with its sky color.
  void SetupCamera(filament::Camera* camera, const RenderCamera& render);
  // Applies RenderSettings (tonemap, exposure, bloom, vignette, GTAO, FXAA).
  void ApplyPostProcess(filament::View* view, const RenderSettings& settings,
                        const RenderCamera& camera);
  // The cameras this frame, like the Vulkan backend's FrameCameras.
  std::vector<RenderCamera> FrameCameras(float width, float height);
  // Reads `target`'s pixels into its CPU buffer (top-down RGBA).
  void ReadOffscreen(Offscreen& target);
  void ReadCaptureSettings();
  void WriteCapture(const std::vector<uint8_t>& pixels, uint32_t width,
                    uint32_t height);

  filament::Engine* engine_ = nullptr;
  filament::Renderer* renderer_ = nullptr;
  filament::SwapChain* swapchain_ = nullptr;
  filament::SwapChain* headless_swap_ = nullptr;  // drives frames offscreen

  filament::View* scene_view_ = nullptr;
  filament::Camera* scene_camera_ = nullptr;
  utils::Entity scene_camera_entity_;
  filament::Skybox* scene_sky_ = nullptr;

  filament::View* ui_view_ = nullptr;
  filament::Camera* ui_camera_ = nullptr;
  utils::Entity ui_camera_entity_;

  // The UI Builder canvas' background (the UI scene's skybox).
  filament::Skybox* ui_preview_sky_ = nullptr;

  fil::FilamentRenderer scene_renderer_;
  fil::FilamentUiRenderer ui_renderer_;

  // The editor's offscreen outputs.
  bool scene_texture_enabled_ = false;
  Offscreen scene_target_;
  Offscreen preview_target_;
  Offscreen ui_preview_target_;
  kEntityId preview_camera_ = entt::null;
  const UI::UIDrawData* ui_preview_data_ = nullptr;
  glm::vec4 ui_preview_background_ = glm::vec4(0.0f);
  bool render_textures_logged_ = false;
  bool skybox_logged_ = false;

  bool show_stats_ = true;
  uint64_t frame_counter_ = 0;

  filament::ColorGrading* color_grading_ = nullptr;
  int cached_tonemap_ = -1;
  float cached_exposure_ = -1.0f;
  std::vector<std::pair<uint64_t, filament::ColorGrading*>>
      garbage_color_gradings_;

  // MERCURY_CAPTURE=<file.png> saves a frame of the window.
  std::string capture_path_;
  uint64_t capture_frame_ = 90;
  bool capture_quit_ = false;
  bool capture_ready_ = false;
  std::vector<uint8_t> capture_pixels_;
  uint32_t capture_width_ = 0;
  uint32_t capture_height_ = 0;
};

}  // namespace Mercury

#endif  // MERCURYENGINE_FILAMENT_APPLICATION_H
