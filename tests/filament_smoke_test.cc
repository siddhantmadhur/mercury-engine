// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "application/default_application.h"
#include "properties/camera.h"
#include "properties/model.h"
#include "properties/render_mesh.h"
#include "properties/transform.h"
#include "scene/scene.h"

using namespace Mercury;

namespace {

class SmokeScene : public GameScene {
 public:
  RenderSettings GetRenderSettings() const override { return settings_; }
  RenderSettings settings_;

  void OnStart() override {
    SetClearColor(glm::vec4(0.05f, 0.1f, 0.25f, 1.0f));
    GetLighting().direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    GetLighting().color = glm::vec3(1.0f);
    GetLighting().ambient = glm::vec3(0.35f);

    if (auto* cam = object_manager_->GetProperty<Camera>(GetPlayerCamera())) {
      cam->SetUseObjectRotation(true);
    }
    if (auto* cam_xform =
            object_manager_->GetProperty<Transform>(GetPlayerCamera())) {
      cam_xform->SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));
      cam_xform->CaptureTickSnapshot();
    }

    // Quad facing +Z in front of the camera with a bright lit orange material.
    std::vector<ModelVertex> vertices(4);
    vertices[0].position = glm::vec3(0.75f, 0.75f, 0.0f);
    vertices[0].normal = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[0].uv = glm::vec2(1.0f, 0.0f);
    vertices[1].position = glm::vec3(0.75f, -0.75f, 0.0f);
    vertices[1].normal = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[1].uv = glm::vec2(1.0f, 1.0f);
    vertices[2].position = glm::vec3(-0.75f, -0.75f, 0.0f);
    vertices[2].normal = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[2].uv = glm::vec2(0.0f, 1.0f);
    vertices[3].position = glm::vec3(-0.75f, 0.75f, 0.0f);
    vertices[3].normal = glm::vec3(0.0f, 0.0f, 1.0f);
    vertices[3].uv = glm::vec2(0.0f, 0.0f);

    RenderMaterial material;
    material.color = glm::vec4(0.95f, 0.45f, 0.1f, 1.0f);
    material.smoothness = 0.6f;

    RenderDraw draw;
    draw.geometry =
        MeshGeometry::Create(std::move(vertices), {0, 3, 1, 1, 3, 2});

    RenderMesh mesh;
    mesh.draws.push_back(std::move(draw));
    mesh.materials.push_back(std::move(material));
    mesh.ComputeBounds();

    const kEntityId quad = object_manager_->NewObject();
    Transform xform(glm::vec3(0.0f));
    xform.CaptureTickSnapshot();
    object_manager_->AddProperty(quad, std::move(xform));
    object_manager_->AddProperty(quad, std::move(mesh));
  }
  void OnFrame(double) override {}
  void OnTick(double) override {}
  void OnDestroy() override {}
};

constexpr int kWidth = 128;
constexpr int kHeight = 128;

struct RenderResult {
  std::vector<uint8_t> pixels;
  filament::math::float2 dynamic_scale{1.0f, 1.0f};
};

// Renders the smoke scene with `settings` and returns the pixels after a few
// extra frames (so TAA history runs) plus the dynamic resolution scale
// Filament applied to the scene viewport.
void RenderSmokeScene(const RenderSettings& settings, RenderResult& result) {
  DefaultApplication app("Filament Smoke", kWidth, kHeight);
  app.EnableHeadlessMode(true);
  auto scene = std::make_unique<SmokeScene>();
  scene->settings_ = settings;
  app.SetGameScene(std::move(scene));
  ASSERT_TRUE(app.StartHosted());

  app.EnableSceneTexture(true);
  app.SetSceneViewportSize(kWidth, kHeight);

  int width = 0;
  int height = 0;
  uint64_t frame_id = 0;
  std::vector<uint8_t>& pixels = result.pixels;
  bool got_pixels = false;
  // Debug builds can take a while to present the first offscreen frame under
  // load, so allow a few seconds rather than a fixed 60 iterations.
  for (int i = 0; i < 600 && !got_pixels; i++) {
    app.StepFrame();
    if (app.CopyScenePixels(width, height, pixels, &frame_id) && frame_id > 0) {
      got_pixels = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  ASSERT_TRUE(got_pixels);
  // Let the TAA history (and the dynamic resolution controller) settle.
  for (int i = 0; i < 5; i++) {
    app.StepFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(app.CopyScenePixels(width, height, pixels, &frame_id));
  EXPECT_EQ(width, kWidth);
  EXPECT_EQ(height, kHeight);
  ASSERT_EQ(pixels.size(), static_cast<size_t>(kWidth) * kHeight * 4);
  result.dynamic_scale = app.SceneViewDynamicResolutionScaleForTest();
}

void ExpectNonUniform(const std::vector<uint8_t>& pixels) {
  uint8_t min_rgb = 255;
  uint8_t max_rgb = 0;
  for (size_t i = 0; i < pixels.size(); i += 4) {
    min_rgb = std::min({min_rgb, pixels[i], pixels[i + 1], pixels[i + 2]});
    max_rgb = std::max({max_rgb, pixels[i], pixels[i + 1], pixels[i + 2]});
  }
  EXPECT_GT(static_cast<int>(max_rgb) - static_cast<int>(min_rgb), 10)
      << "Rendered frame appears uniform/blank (min=" << int(min_rgb)
      << ", max=" << int(max_rgb) << ")";
}

double MeanAbsDiff(const std::vector<uint8_t>& a,
                   const std::vector<uint8_t>& b) {
  double sum = 0.0;
  size_t count = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); i++) {
    if (i % 4 == 3) continue;  // alpha
    sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    count++;
  }
  return count ? sum / static_cast<double>(count) : 0.0;
}

RenderSettings MakeSettings(RenderSettings::AntiAliasing aa, bool fsr) {
  RenderSettings settings;
  settings.anti_aliasing = aa;
  settings.fsr = fsr;
  settings.render_scale = 0.5f;
  return settings;
}

TEST(FilamentSmokeTest, HeadlessOffscreenRenderIsNonUniform) {
  RenderResult result;
  RenderSmokeScene(RenderSettings{}, result);
  ExpectNonUniform(result.pixels);
}

TEST(FilamentSmokeTest, FxaaRendersFullSizeFrame) {
  RenderResult result;
  RenderSmokeScene(MakeSettings(RenderSettings::AntiAliasing::kFxaa, false),
                   result);
  ExpectNonUniform(result.pixels);
}

TEST(FilamentSmokeTest, TaaWithFsrRendersFullSizeFrame) {
  RenderResult off;
  RenderSmokeScene(MakeSettings(RenderSettings::AntiAliasing::kTaa, false),
                   off);
  ExpectNonUniform(off.pixels);
  EXPECT_FLOAT_EQ(off.dynamic_scale.x, 1.0f);
  EXPECT_FLOAT_EQ(off.dynamic_scale.y, 1.0f);

  RenderResult on;
  RenderSmokeScene(MakeSettings(RenderSettings::AntiAliasing::kTaa, true), on);
  ExpectNonUniform(on.pixels);
  EXPECT_FLOAT_EQ(on.dynamic_scale.x, 0.5f);
  EXPECT_FLOAT_EQ(on.dynamic_scale.y, 0.5f);
}

TEST(FilamentSmokeTest, FsrChangesThePixels) {
  RenderResult off;
  RenderSmokeScene(MakeSettings(RenderSettings::AntiAliasing::kNone, false),
                   off);
  RenderResult on;
  RenderSmokeScene(MakeSettings(RenderSettings::AntiAliasing::kNone, true), on);
  ASSERT_EQ(off.pixels.size(), on.pixels.size());
  EXPECT_FLOAT_EQ(off.dynamic_scale.x, 1.0f);
  EXPECT_FLOAT_EQ(on.dynamic_scale.x, 0.5f);
  const double diff = MeanAbsDiff(off.pixels, on.pixels);
  std::cout << "FSR on/off mean abs pixel diff: " << diff << "\n";
  EXPECT_GT(diff, 0.05);
}

// A closed room (red wall at -X, blue at +X, grey elsewhere) around a smooth
// metal sphere, lit by a LightProbeVolume at the room's center. The sphere
// has nothing but the probe's capture to reflect, so its left side must show
// the red wall and its right side the blue one.
class ProbeRoomScene : public GameScene {
 public:
  RenderSettings GetRenderSettings() const override {
    RenderSettings settings;
    settings.anti_aliasing = RenderSettings::AntiAliasing::kNone;
    return settings;
  }

  void OnStart() override {
    SetClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    SceneLighting& lighting = GetLighting();
    lighting.direction = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.3f));
    lighting.color = glm::vec3(1.0f);
    // The ambient light only reaches the final image through the probe's
    // bounces, so make it strong.
    lighting.ambient = glm::vec3(1.0f);
    lighting.ambient_ground = glm::vec3(1.0f);
    lighting.probes.enabled = true;
    lighting.probes.placed = true;
    // Above the sphere, so the capture sees the room and not the sphere.
    lighting.probes.min = lighting.probes.max = glm::vec3(0.0f, 2.5f, 0.0f);
    lighting.probes.intensity = 3.0f;
    lighting.probes.bounces = 2;
    lighting.probes.capture_size = 64;

    if (auto* cam = object_manager_->GetProperty<Camera>(GetPlayerCamera())) {
      cam->SetUseObjectRotation(true);
    }
    if (auto* cam_xform =
            object_manager_->GetProperty<Transform>(GetPlayerCamera())) {
      cam_xform->SetPosition(glm::vec3(0.0f, 0.0f, 3.0f));
      cam_xform->CaptureTickSnapshot();
    }

    const glm::vec4 grey(0.7f, 0.7f, 0.7f, 1.0f);
    AddWall({-kHalf, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0},
            {0.9f, 0.05f, 0.05f, 1.0f});
    AddWall({kHalf, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0},
            {0.05f, 0.05f, 0.9f, 1.0f});
    AddWall({0, -kHalf, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, grey);
    AddWall({0, kHalf, 0}, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}, grey);
    AddWall({0, 0, -kHalf}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, grey);
    AddWall({0, 0, kHalf}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, grey);
    AddSphere(1.0f);
  }
  void OnFrame(double) override {}
  void OnTick(double) override {}
  void OnDestroy() override {}

 private:
  static constexpr float kHalf = 4.0f;

  void AddMesh(std::vector<ModelVertex> vertices, std::vector<uint32_t> indices,
               RenderMaterial material) {
    RenderDraw draw;
    draw.geometry =
        MeshGeometry::Create(std::move(vertices), std::move(indices));
    RenderMesh mesh;
    mesh.draws.push_back(std::move(draw));
    mesh.materials.push_back(std::move(material));
    mesh.ComputeBounds();
    const kEntityId entity = object_manager_->NewObject();
    Transform xform(glm::vec3(0.0f));
    xform.CaptureTickSnapshot();
    object_manager_->AddProperty(entity, std::move(xform));
    object_manager_->AddProperty(entity, std::move(mesh));
  }

  void AddWall(glm::vec3 center, glm::vec3 u, glm::vec3 v, glm::vec3 normal,
               glm::vec4 color) {
    std::vector<ModelVertex> vertices(4);
    const glm::vec2 corners[4] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (int i = 0; i < 4; i++) {
      vertices[i].position =
          center + u * corners[i].x * kHalf + v * corners[i].y * kHalf;
      vertices[i].normal = normal;
      vertices[i].tangent = glm::vec4(u, 1.0f);
      vertices[i].uv = glm::vec2(0.0f);
    }
    RenderMaterial material;
    material.color = color;
    material.smoothness = 0.0f;
    material.double_sided = true;
    // Counter-clockwise as seen along the normal.
    const bool flip = glm::dot(glm::cross(u, v), normal) < 0.0f;
    AddMesh(std::move(vertices),
            flip ? std::vector<uint32_t>{0, 1, 3, 1, 2, 3}
                 : std::vector<uint32_t>{0, 3, 1, 1, 3, 2},
            material);
  }

  void AddSphere(float radius) {
    constexpr int kSegments = 48;
    constexpr int kRings = 24;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    for (int ring = 0; ring <= kRings; ring++) {
      const float phi = glm::pi<float>() * ring / kRings;
      for (int seg = 0; seg <= kSegments; seg++) {
        const float theta = 2.0f * glm::pi<float>() * seg / kSegments;
        const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                          std::sin(phi) * std::sin(theta));
        ModelVertex vertex;
        vertex.position = n * radius;
        vertex.normal = n;
        vertex.tangent =
            glm::vec4(-std::sin(theta), 0.0f, std::cos(theta), 1.0f);
        vertex.uv = glm::vec2(static_cast<float>(seg) / kSegments,
                              static_cast<float>(ring) / kRings);
        vertices.push_back(vertex);
      }
    }
    for (int ring = 0; ring < kRings; ring++) {
      for (int seg = 0; seg < kSegments; seg++) {
        const uint32_t a = ring * (kSegments + 1) + seg;
        const uint32_t b = a + kSegments + 1;
        indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
      }
    }
    RenderMaterial material;
    material.color = glm::vec4(1.0f);
    material.metallic = 1.0f;
    material.smoothness = 0.98f;
    material.double_sided = true;
    AddMesh(std::move(vertices), std::move(indices), material);
  }
};

}  // namespace

TEST(FilamentSmokeTest, ProbeCaptureLightsAndReflectsTheRoom) {
  constexpr int kSize = 128;
  DefaultApplication app("Filament Probe", kSize, kSize);
  app.EnableHeadlessMode(true);
  app.SetGameScene(std::make_unique<ProbeRoomScene>());
  ASSERT_TRUE(app.StartHosted());
  app.EnableSceneTexture(true);
  app.SetSceneViewportSize(kSize, kSize);

  // The probe is captured a few frames after the scene loads; wait for a
  // frame well after that.
  int width = 0;
  int height = 0;
  uint64_t frame_id = 0;
  std::vector<uint8_t> pixels;
  bool got_pixels = false;
  for (int i = 0; i < 1200 && !got_pixels; i++) {
    app.StepFrame();
    got_pixels =
        app.CopyScenePixels(width, height, pixels, &frame_id) && frame_id >= 20;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_TRUE(got_pixels);
  ASSERT_EQ(pixels.size(), static_cast<size_t>(kSize) * kSize * 4);

  // Mean color of a patch around (cx, cy).
  auto patch = [&](int cx, int cy, int half) {
    glm::vec3 sum(0.0f);
    int count = 0;
    for (int y = cy - half; y <= cy + half; y++) {
      for (int x = cx - half; x <= cx + half; x++) {
        const size_t i = (static_cast<size_t>(y) * kSize + x) * 4;
        sum += glm::vec3(pixels[i], pixels[i + 1], pixels[i + 2]);
        count++;
      }
    }
    return sum / static_cast<float>(count);
  };
  // The sphere fills the middle of the frame; its left and right flanks
  // mirror the -X and +X walls.
  const glm::vec3 left = patch(kSize / 2 - 22, kSize / 2, 4);
  const glm::vec3 right = patch(kSize / 2 + 22, kSize / 2, 4);
  EXPECT_GT(left.r, left.b + 15.0f)
      << "left flank should reflect the red wall: " << left.r << "," << left.g
      << "," << left.b;
  EXPECT_GT(right.b, right.r + 15.0f)
      << "right flank should reflect the blue wall: " << right.r << ","
      << right.g << "," << right.b;
  // Lit by the probe, not black.
  EXPECT_GT(glm::max(left.r, right.b), 40.0f);
}
