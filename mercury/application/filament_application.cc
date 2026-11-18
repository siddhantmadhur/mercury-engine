// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "filament_application.h"

#include <backend/PixelBufferDescriptor.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <stb/stb_image_write.h>

#include "components/components.h"
#include "filament/metal_layer.h"
#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "properties/camera.h"
#include "properties/transform.h"

namespace Mercury {

namespace {

float SrgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 SrgbToLinear(const glm::vec3& c) {
  return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)};
}

filament::math::float3 ToFilament(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}
filament::math::float4 ToFilament(const glm::vec4& v) {
  return {v.x, v.y, v.z, v.w};
}

// Filament takes column-major matrices, like glm.
// Filament's matrices are column-major like glm's, so the scalar
// constructor takes glm's column order directly.
filament::math::mat4 ToFilament(const glm::mat4& m) {
  return filament::math::mat4(
      m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3],
      m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3]);
}

// Readbacks hand back bottom-up rows; flip them into `out` (top-down).
void FlipRows(const uint8_t* bottom_up, uint8_t* out, uint32_t width,
              uint32_t height) {
  const size_t stride = size_t{width} * 4;
  for (uint32_t y = 0; y < height; y++) {
    std::memcpy(out + y * stride, bottom_up + size_t{height - 1 - y} * stride,
                stride);
  }
}

}  // namespace

FilamentApplication::FilamentApplication(const std::string& window_title,
                                         float initial_width,
                                         float initial_height)
    : Application(window_title, initial_width, initial_height) {}

FilamentApplication::~FilamentApplication() {
  if (engine_) {
    engine_->flushAndWait();
    DestroyOffscreen(scene_target_);
    DestroyOffscreen(preview_target_);
    DestroyOffscreen(ui_preview_target_);
    engine_->destroy(ui_view_);
    engine_->destroyCameraComponent(ui_camera_entity_);
    engine_->destroy(scene_view_);
    engine_->destroyCameraComponent(scene_camera_entity_);
    scene_renderer_.scene()->setSkybox(nullptr);
    engine_->destroy(scene_sky_);
    ui_renderer_.scene()->setSkybox(nullptr);
    engine_->destroy(ui_preview_sky_);
    ui_renderer_.Shutdown(engine_);
    scene_renderer_.Shutdown(engine_);
    engine_->destroy(renderer_);
    engine_->destroy(swapchain_);
    engine_->destroy(headless_swap_);
    // Nothing above may still be referenced by a frame when the engine frees
    // what is left.
    engine_->flushAndWait();
    // Filament's teardown of a Vulkan engine panics on MoltenVK
    // ("texture used after it has been freed") when the last frame's
    // descriptor sets are still cached, so the engine is intentionally left
    // for the OS to reclaim at process exit. Nothing else can use it after
    // this point.
    engine_ = nullptr;
  }
}

bool FilamentApplication::Initialize() {
  if (!Application::Initialize()) return false;

  // A bundled app carries its own MoltenVK; the loader has to be told
  // before Filament looks for a Vulkan driver.
  os::UseBundledVulkanDriver();
  engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
  if (!engine_) {
    PDebug::error("Filament: could not create the Vulkan backend");
    return false;
  }
  renderer_ = engine_->createRenderer();

  if (!IsHeadless()) {
    void* layer = fil::CocoaGetMetalLayer(GetWindow());
#if defined(__APPLE__)
    if (!layer) {
      PDebug::error("Filament: no metal layer for the window");
      return false;
    }
#endif
    swapchain_ = engine_->createSwapChain(layer);
  } else {
    // No window: a 1x1 headless SwapChain drives the frame lifecycle.
    headless_swap_ = engine_->createSwapChain(1, 1);
  }
  return true;
}

bool FilamentApplication::OnLoad() {
  if (!game_scene_) {
    PDebug::error("Filament: no game scene set before load");
    return false;
  }
  if (!scene_renderer_.Init(engine_)) {
    PDebug::error("Filament: could not init the scene renderer");
    return false;
  }
  if (!ui_renderer_.Init(engine_)) {
    PDebug::error("Filament: could not init the UI renderer");
    return false;
  }

  // The window's scene view.
  scene_camera_entity_ = utils::EntityManager::get().create();
  scene_camera_ = engine_->createCamera(scene_camera_entity_);
  scene_view_ = engine_->createView();
  scene_view_->setScene(scene_renderer_.scene());
  scene_view_->setCamera(scene_camera_);
  scene_sky_ =
      filament::Skybox::Builder().color({0.1f, 0.1f, 0.1f, 1.0f}).build(*engine_);
  scene_renderer_.scene()->setSkybox(scene_sky_);
  scene_view_->setAntiAliasing(filament::View::AntiAliasing::FXAA);

  // The game's UI drawn over the scene.
  ui_camera_entity_ = utils::EntityManager::get().create();
  ui_camera_ = engine_->createCamera(ui_camera_entity_);
  ui_view_ = engine_->createView();
  ui_view_->setScene(ui_renderer_.scene());
  ui_view_->setCamera(ui_camera_);
  ui_view_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
  // The UI Builder canvas renders the same scene on its own, opaque, with
  // the canvas' background color behind it.
  ui_preview_sky_ = filament::Skybox::Builder().build(*engine_);
  ui_renderer_.scene()->setSkybox(ui_preview_sky_);

  ReadCaptureSettings();
  return true;
}

std::vector<RenderCamera> FilamentApplication::FrameCameras(float width,
                                                            float height) {
  std::vector<RenderCamera> cameras =
      game_scene_->GetRenderCameras(width, height);
  if (!cameras.empty()) return cameras;

  // The editor's camera, or a scene built in code: the current camera.
  RenderCamera camera;
  glm::mat4x4 view_proj, sky;
  glm::vec3 position(0.0f);
  const kEntityId entity = game_scene_->GetPlayerCamera();
  auto& registry = game_scene_->GetRegistry();
  auto* cam = registry.try_get<Camera>(entity);
  auto* transform = registry.try_get<Transform>(entity);
  if (cam && transform) {
    camera.view = cam->GetView(*transform);
    camera.projection = cam->GetProjection(width, height);
    camera.position = transform->GetInterpolatedPosition();
  } else if (game_scene_->GetCameraMatrices(entity, width, height, view_proj,
                                            sky, position)) {
    camera.projection = view_proj;
    camera.position = position;
  }
  camera.entity = entity;
  camera.background = game_scene_->GetClearColor();
  camera.clear = static_cast<int>(CameraClear::kColor);
  cameras.push_back(camera);
  return cameras;
}

void FilamentApplication::SetupCamera(filament::Camera* camera,
                                     const RenderCamera& render) {
  camera->setCustomProjection(
      ToFilament(render.projection), std::max(render.near_clip, 1e-4f),
      std::max(render.far_clip, render.near_clip * 2.0f + 1.0f));
  camera->setModelMatrix(ToFilament(glm::inverse(render.view)));
}

void FilamentApplication::EnsureOffscreen(Offscreen& target, uint32_t width,
                                          uint32_t height,
                                          filament::Scene* scene, bool is_ui) {
  if (target.target && target.width == width && target.height == height &&
      target.view->getScene() == scene) {
    return;
  }
  DestroyOffscreen(target);
  target.width = width;
  target.height = height;

  target.color =
      filament::Texture::Builder()
          .width(width)
          .height(height)
          .levels(1)
          .usage(filament::Texture::Usage::COLOR_ATTACHMENT |
                 filament::Texture::Usage::SAMPLEABLE)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(filament::Texture::InternalFormat::RGBA8)
          .build(*engine_);
  target.depth =
      filament::Texture::Builder()
          .width(width)
          .height(height)
          .levels(1)
          .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(filament::Texture::InternalFormat::DEPTH24)
          .build(*engine_);
  target.target = filament::RenderTarget::Builder()
                      .texture(
                          filament::RenderTarget::AttachmentPoint::COLOR0,
                          target.color)
                      .texture(
                          filament::RenderTarget::AttachmentPoint::DEPTH,
                          target.depth)
                      .build(*engine_);
  target.camera_entity = utils::EntityManager::get().create();
  target.camera = engine_->createCamera(target.camera_entity);
  target.view = engine_->createView();
  target.view->setScene(scene);
  target.view->setCamera(target.camera);
  target.view->setRenderTarget(target.target);
  target.view->setViewport(
      filament::Viewport(0, 0, width, height));
  target.view->setAntiAliasing(filament::View::AntiAliasing::FXAA);
  if (is_ui) {
    target.view->setBlendMode(filament::View::BlendMode::OPAQUE);
  }
}

void FilamentApplication::DestroyOffscreen(Offscreen& target) {
  if (!target.target) return;
  engine_->destroy(target.view);
  engine_->destroyCameraComponent(target.camera_entity);
  engine_->destroy(target.target);
  engine_->destroy(target.color);
  engine_->destroy(target.depth);
  target.target = nullptr;
  target.color = target.depth = nullptr;
  target.view = nullptr;
  target.camera = nullptr;
  target.camera_entity = {};
  target.has_pixels = false;
}

void FilamentApplication::ReadOffscreen(Offscreen& target) {
  if (!target.target) return;
  const size_t bytes = size_t{target.width} * target.height * 4;
  if (target.pixels.size() != bytes) target.pixels.resize(bytes);

  auto* row = new uint8_t[bytes];
  renderer_->readPixels(
      target.target, 0, 0, target.width, target.height,
      filament::backend::PixelBufferDescriptor(
          row, bytes, filament::backend::PixelDataFormat::RGBA,
          filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
          [](void* buffer, size_t size, void* user) {
            auto* target = static_cast<Offscreen*>(user);
            std::memcpy(target->pixels.data(), buffer, size);
            delete[] static_cast<uint8_t*>(buffer);
          },
          &target));
  // The callback has fired by the time flushAndWait() below returns; the
  // bottom-up rows are flipped then.
  pending_flips_.push_back(&target);
  target.has_pixels = true;
  target.frame_id = frame_counter_;
}

bool FilamentApplication::CopyOffscreen(Offscreen& target, int& width,
                                        int& height,
                                        std::vector<uint8_t>& rgba_out,
                                        uint64_t* frame_id) {
  if (!target.has_pixels) return false;
  rgba_out = target.pixels;
  width = static_cast<int>(target.width);
  height = static_cast<int>(target.height);
  if (frame_id) *frame_id = target.frame_id;
  return true;
}

void FilamentApplication::Render() {
  if (!engine_ || !game_scene_) return;
  frame_counter_++;

  scene_renderer_.SyncScene(*game_scene_);

  const UI::UIDrawData* game_ui = nullptr;
  if (!IsHeadless() && !scene_texture_enabled_) {
    game_ui = game_scene_->GetUIDrawData();
  }
  ui_renderer_.Prepare(engine_, game_ui ? *game_ui : UI::UIDrawData{});

  filament::SwapChain* frame_swap = IsHeadless() ? headless_swap_ : swapchain_;
  if (!frame_swap || !renderer_->beginFrame(frame_swap)) return;

  const uint32_t window_width = static_cast<uint32_t>(GetWidth());
  const uint32_t window_height = static_cast<uint32_t>(GetHeight());
  const uint32_t view_width = scene_texture_enabled_ && scene_target_.target
                                  ? scene_target_.width
                                  : window_width;
  const uint32_t view_height = scene_texture_enabled_ && scene_target_.target
                                   ? scene_target_.height
                                   : window_height;
  std::vector<RenderCamera> cameras = FrameCameras(
      static_cast<float>(view_width), static_cast<float>(view_height));

  // Features still on the way (each is only reported once).
  for (const RenderCamera& camera : cameras) {
    if (!camera.target_texture.empty() && !render_textures_logged_) {
      render_textures_logged_ = true;
      PDebug::warn("Filament: RenderTexture cameras are not supported yet");
    }
  }
  if (!game_scene_->GetSkyboxPath().empty() && !skybox_logged_) {
    skybox_logged_ = true;
    PDebug::warn(
        "Filament: skybox panoramas are not supported yet; using the "
        "camera's background color");
  }

  const RenderCamera* main = nullptr;
  for (const RenderCamera& camera : cameras) {
    if (camera.target_texture.empty() && !camera.overlay) {
      main = &camera;
      break;
    }
  }

  // The window (when there is one) shows the main camera, with the game's
  // UI over it.
  if (!IsHeadless() && main) {
    const glm::vec4 bg = glm::clamp(main->background, 0.0f, 1.0f);
    scene_sky_->setColor(
        ToFilament(glm::vec4(SrgbToLinear(glm::vec3(bg)), 1.0f)));
    SetupCamera(scene_camera_, *main);
    scene_view_->setViewport(
        filament::Viewport(0, 0, window_width, window_height));
    scene_view_->setVisibleLayers(0xff, main->culling_mask & 0xff);
    renderer_->render(scene_view_);
    if (game_ui) {
      // The UI scene's skybox belongs to the UI Builder canvas; over the
      // window the UI must be a pure overlay.
      ui_renderer_.scene()->setSkybox(nullptr);
      ui_view_->setViewport(scene_view_->getViewport());
      renderer_->render(ui_view_);
    }
  }

  // The editor's scene viewport.
  if (scene_texture_enabled_ && scene_target_.target) {
    if (main) {
      const glm::vec4 bg = glm::clamp(main->background, 0.0f, 1.0f);
      scene_sky_->setColor(
          ToFilament(glm::vec4(SrgbToLinear(glm::vec3(bg)), 1.0f)));
      SetupCamera(scene_target_.camera, *main);
      scene_target_.view->setVisibleLayers(0xff, main->culling_mask & 0xff);
      renderer_->render(scene_target_.view);
      ReadOffscreen(scene_target_);
    }
  }

  // The editor's camera preview.
  if (preview_camera_ != entt::null && preview_target_.target) {
    RenderCamera preview;
    glm::mat4x4 view_proj, sky;
    glm::vec3 position(0.0f);
    auto& registry = game_scene_->GetRegistry();
    auto* cam = registry.try_get<Camera>(preview_camera_);
    auto* transform = registry.try_get<Transform>(preview_camera_);
    const float w = static_cast<float>(preview_target_.width);
    const float h = static_cast<float>(preview_target_.height);
    if (cam && transform && game_scene_->GetCameraMatrices(preview_camera_, w,
                                                           h, view_proj, sky,
                                                           position)) {
      preview.entity = preview_camera_;
      preview.view = cam->GetView(*transform);
      preview.projection = cam->GetProjection(w, h);
      preview.position = position;
      preview.background = game_scene_->GetClearColor();
      if (auto* settings =
              registry.try_get<CameraComponent>(preview_camera_)) {
        preview.background = settings->background;
        preview.culling_mask = static_cast<uint32_t>(settings->culling_mask);
        preview.near_clip = settings->near_clip;
        preview.far_clip = settings->far_clip;
      }
      SetupCamera(preview_target_.camera, preview);
      preview_target_.view->setVisibleLayers(0xff,
                                             preview.culling_mask & 0xff);
      renderer_->render(preview_target_.view);
      ReadOffscreen(preview_target_);
    }
  }

  // The editor's UI Builder canvas.
  if (ui_preview_data_ && ui_preview_target_.target) {
    ui_renderer_.Prepare(engine_, *ui_preview_data_);
    const glm::vec4 bg = glm::clamp(ui_preview_background_, 0.0f, 1.0f);
    ui_preview_sky_->setColor(
        ToFilament(glm::vec4(SrgbToLinear(glm::vec3(bg)), bg.a)));
    ui_renderer_.scene()->setSkybox(ui_preview_sky_);
    renderer_->render(ui_preview_target_.view);
    ui_renderer_.scene()->setSkybox(nullptr);
    ReadOffscreen(ui_preview_target_);
    ui_renderer_.Prepare(engine_, game_ui ? *game_ui : UI::UIDrawData{});
  }

  // A capture of the window, for scripted tests.
  const bool capturing = !IsHeadless() && !capture_path_.empty() &&
                         frame_counter_ >= capture_frame_ && !capture_ready_;
  if (capturing) {
    const size_t bytes = size_t{window_width} * window_height * 4;
    capture_pixels_.resize(bytes);
    renderer_->readPixels(
        0, 0, window_width, window_height,
        filament::backend::PixelBufferDescriptor(
            new uint8_t[bytes], bytes, filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
            [](void* buffer, size_t size, void* user) {
              auto* self = static_cast<FilamentApplication*>(user);
              if (self) {
                std::memcpy(self->capture_pixels_.data(), buffer, size);
                self->capture_ready_ = true;
              }
              delete[] static_cast<uint8_t*>(buffer);
            },
            this));
    capture_width_ = window_width;
    capture_height_ = window_height;
  }

  renderer_->endFrame();
  engine_->flushAndWait();

  // The readback callbacks have all fired by now; turn the bottom-up
  // buffers into the top-down rows everyone else expects.
  for (Offscreen* target : pending_flips_) {
    const size_t bytes = size_t{target->width} * target->height * 4;
    auto* flipped = new uint8_t[bytes];
    FlipRows(target->pixels.data(), flipped, target->width, target->height);
    std::memcpy(target->pixels.data(), flipped, bytes);
    delete[] flipped;
  }
  pending_flips_.clear();

  if (capturing && capture_ready_) {
    auto* flipped = new uint8_t[capture_pixels_.size()];
    FlipRows(capture_pixels_.data(), flipped, capture_width_,
             capture_height_);
    std::vector<uint8_t> top_down(flipped, flipped + capture_pixels_.size());
    delete[] flipped;
    WriteCapture(top_down, capture_width_, capture_height_);
  }

  preview_camera_ = entt::null;
  ui_preview_data_ = nullptr;
}

void FilamentApplication::OnResize(int32_t width, int32_t height) {
  Application::OnResize(width, height);
  // The scene view follows the window; nothing to rebuild.
}

void FilamentApplication::EnableSceneTexture(bool enabled) {
  scene_texture_enabled_ = enabled;
  if (game_scene_) {
    const float w =
        enabled && scene_target_.width
            ? static_cast<float>(scene_target_.width)
            : static_cast<float>(GetWidth());
    const float h = enabled && scene_target_.height
                        ? static_cast<float>(scene_target_.height)
                        : static_cast<float>(GetHeight());
    game_scene_->SetWidth(w);
    game_scene_->SetHeight(h);
  }
}

void FilamentApplication::SetSceneViewportSize(int width, int height) {
  const uint32_t w = static_cast<uint32_t>(std::max(1, width));
  const uint32_t h = static_cast<uint32_t>(std::max(1, height));
  EnsureOffscreen(scene_target_, w, h, scene_renderer_.scene(), false);
  if (scene_texture_enabled_ && game_scene_) {
    game_scene_->SetWidth(static_cast<float>(w));
    game_scene_->SetHeight(static_cast<float>(h));
  }
}

bool FilamentApplication::CopyScenePixels(int& width, int& height,
                                          std::vector<uint8_t>& rgba_out,
                                          uint64_t* frame_id) {
  return CopyOffscreen(scene_target_, width, height, rgba_out, frame_id);
}

void FilamentApplication::SetPreviewCamera(kEntityId camera, int width,
                                            int height) {
  preview_camera_ = camera;
  const uint32_t w = static_cast<uint32_t>(std::max(1, width));
  const uint32_t h = static_cast<uint32_t>(std::max(1, height));
  EnsureOffscreen(preview_target_, w, h, scene_renderer_.scene(), false);
}

bool FilamentApplication::CopyPreviewPixels(int& width, int& height,
                                            std::vector<uint8_t>& rgba_out,
                                            uint64_t* frame_id) {
  return CopyOffscreen(preview_target_, width, height, rgba_out, frame_id);
}

void FilamentApplication::SetUIPreview(const UI::UIDrawData* draw_data,
                                       int width, int height,
                                       glm::vec4 background) {
  ui_preview_data_ = draw_data;
  ui_preview_background_ = background;
  if (!draw_data) return;
  const uint32_t w = static_cast<uint32_t>(std::clamp(width, 1, 8192));
  const uint32_t h = static_cast<uint32_t>(std::clamp(height, 1, 8192));
  EnsureOffscreen(ui_preview_target_, w, h, ui_renderer_.scene(), true);
}

bool FilamentApplication::CopyUIPreviewPixels(int& width, int& height,
                                              std::vector<uint8_t>& rgba_out,
                                              uint64_t* frame_id) {
  return CopyOffscreen(ui_preview_target_, width, height, rgba_out, frame_id);
}

void FilamentApplication::RequestCapture(const std::string& file) {
  capture_path_ = file;
  capture_frame_ = frame_counter_;
  capture_quit_ = false;
}

void FilamentApplication::ReadCaptureSettings() {
  if (const char* path = std::getenv("MERCURY_CAPTURE")) capture_path_ = path;
  if (const char* frame = std::getenv("MERCURY_CAPTURE_FRAME")) {
    capture_frame_ = std::strtoull(frame, nullptr, 10);
  }
  if (const char* quit = std::getenv("MERCURY_CAPTURE_QUIT")) {
    capture_quit_ = std::string(quit) == "1";
  }
}

void FilamentApplication::WriteCapture(const std::vector<uint8_t>& pixels,
                                       uint32_t width, uint32_t height) {
  {
    std::error_code ec;
    const std::filesystem::path parent =
        std::filesystem::path(capture_path_).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  }
  if (stbi_write_png(capture_path_.c_str(), static_cast<int>(width),
                     static_cast<int>(height), 4, pixels.data(),
                     static_cast<int>(width) * 4)) {
    PDebug::info("Saved capture to {}", capture_path_);
  } else {
    PDebug::error("Could not write capture to {}", capture_path_);
  }
  capture_path_.clear();
  if (capture_quit_) Quit();
}

}  // namespace Mercury
