// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
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

TEST(FilamentSmokeTest, HeadlessOffscreenRenderIsNonUniform) {
  constexpr int kWidth = 128;
  constexpr int kHeight = 128;

  DefaultApplication app("Filament Smoke", kWidth, kHeight);
  app.EnableHeadlessMode(true);
  app.SetGameScene(std::make_unique<SmokeScene>());
  ASSERT_TRUE(app.StartHosted());

  app.EnableSceneTexture(true);
  app.SetSceneViewportSize(kWidth, kHeight);

  int width = 0;
  int height = 0;
  uint64_t frame_id = 0;
  std::vector<uint8_t> pixels;
  bool got_pixels = false;
  for (int i = 0; i < 60 && !got_pixels; i++) {
    app.StepFrame();
    if (app.CopyScenePixels(width, height, pixels, &frame_id) && frame_id > 0) {
      got_pixels = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  ASSERT_TRUE(got_pixels);
  EXPECT_EQ(width, kWidth);
  EXPECT_EQ(height, kHeight);
  ASSERT_EQ(pixels.size(), static_cast<size_t>(kWidth) * kHeight * 4);

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

}  // namespace
