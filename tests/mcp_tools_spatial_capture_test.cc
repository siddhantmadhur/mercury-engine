// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * mcp_tools_spatial_capture_test.cc
 *
 * Tests of the T4 spatial-query and viewport-capture tools: edit-mode world
 * bounds (including the model-node fallback), nodes_in_box, oriented-box rays
 * with layer/inactive filtering, the ViewState save/restore guard, software
 * overlays, PNG output, and a real headless-Filament capture.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <stb/stb_image.h>

#include "application/default_application.h"
#include "components/components.h"
#include "editor_scene.h"
#include "mcp_tools_fixture.h"

using namespace Mercury;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;

namespace {

// A render host that hands out a synthetic, uniform RGBA frame. The tools
// draw their overlays into the copy they take from it, so tests can compare
// the encoded PNG against the known fill colour.
class FakeRenderHost : public RenderHost {
 public:
  bool fail = false;
  int width = 0;
  int height = 0;

  void SetSceneViewportSize(int w, int h) override {
    width = w;
    height = h;
  }

  bool CopyScenePixels(int& w, int& h, std::vector<uint8_t>& rgba,
                       uint64_t* frame_id = nullptr) override {
    if (fail) return false;
    w = width > 0 ? width : 64;
    h = height > 0 ? height : 64;
    rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) {
      rgba[i + 0] = 10;
      rgba[i + 1] = 20;
      rgba[i + 2] = 30;
      rgba[i + 3] = 255;
    }
    if (frame_id) *frame_id = ++frame_;
    return true;
  }

 private:
  uint64_t frame_ = 0;
};

std::string ErrorText(const ToolResult& result) {
  return result.result.contains("error")
             ? result.result["error"].get<std::string>()
             : std::string();
}

bool HasHit(const ToolResult& result, const std::string& name) {
  if (!result.ok || !result.result.contains("hits")) return false;
  for (const auto& hit : result.result["hits"]) {
    if (hit["name"] == name) return true;
  }
  return false;
}

}  // namespace

TEST(McpToolsSpatialCaptureTest, BoundsOfColliderAndModelNodes) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();

  // A collider node: the transform's scale is baked into the world AABB and
  // the local box keeps the unscaled collider extent.
  ToolResult ground =
      fixture.CallTool("query_scene_spatial",
                       {{"mode", "bounds"}, {"node", {{"path", "Ground"}}}});
  ASSERT_TRUE(ground.ok) << ground.result.dump();
  EXPECT_EQ(ground.result["node"]["name"], "Ground");
  const auto gmin = ground.result["world_min"];
  const auto gmax = ground.result["world_max"];
  ASSERT_EQ(gmin.size(), 3u);
  EXPECT_LT(gmin[0].get<float>(), gmax[0].get<float>());
  EXPECT_LT(gmin[1].get<float>(), gmax[1].get<float>());
  EXPECT_NEAR(gmax[1].get<float>(), 0.0f, 1e-3f);  // top of the floor
  EXPECT_EQ(ground.result["model"].size(), 16u);

  // A MeshRenderer-only node: no collider, so this exercises the RenderMesh
  // bounds_min/bounds_max fallback in GetNodeWorldBounds.
  MapNode* quad =
      scene.CreatePreset(scene.GetMap().GetRoot(), EditorScene::Preset::kQuad);
  ASSERT_NE(quad, nullptr);
  scene.OnFrame(0.0);
  ToolResult model =
      fixture.CallTool("query_scene_spatial",
                       {{"mode", "bounds"}, {"node", {{"uid", quad->uid}}}});
  ASSERT_TRUE(model.ok) << model.result.dump();
  EXPECT_LT(model.result["world_min"][0].get<float>(),
            model.result["world_max"][0].get<float>());
  EXPECT_LT(model.result["world_min"][1].get<float>(),
            model.result["world_max"][1].get<float>());

  ToolResult missing = fixture.CallTool(
      "query_scene_spatial",
      {{"mode", "bounds"}, {"node", {{"path", "Root/NoSuchNode"}}}});
  EXPECT_FALSE(missing.ok);
  EXPECT_FALSE(ErrorText(missing).empty());
}

TEST(McpToolsSpatialCaptureTest, NodesInBoxHonoursLayerMask) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  MapNode* player = scene.GetMap().FindNode("Player");
  ASSERT_NE(player, nullptr);
  scene.SetLayer(scene.GetEntityForNode(player->uid), 3);

  const nlohmann::json args = {
      {"mode", "nodes_in_box"},
      {"box", {{"min", {-30, -3, -30}}, {"max", {30, 3, 30}}}},
  };
  ToolResult all = fixture.CallTool("query_scene_spatial", args);
  ASSERT_TRUE(all.ok) << all.result.dump();
  EXPECT_GT(all.result["count"].get<size_t>(), 1u);
  bool found_player = false;
  for (const auto& node : all.result["nodes"]) {
    if (node["name"] == "Player") found_player = true;
  }
  EXPECT_TRUE(found_player);

  nlohmann::json layer_zero = args;
  layer_zero["layer_mask"] = 1;
  ToolResult excluded = fixture.CallTool("query_scene_spatial", layer_zero);
  ASSERT_TRUE(excluded.ok) << excluded.result.dump();
  for (const auto& node : excluded.result["nodes"]) {
    EXPECT_NE(node["name"], "Player");
  }

  nlohmann::json layer_three = args;
  layer_three["layer_mask"] = 1u << 3;
  ToolResult included = fixture.CallTool("query_scene_spatial", layer_three);
  ASSERT_TRUE(included.ok) << included.result.dump();
  found_player = false;
  for (const auto& node : included.result["nodes"]) {
    if (node["name"] == "Player") found_player = true;
  }
  EXPECT_TRUE(found_player);
}

TEST(McpToolsSpatialCaptureTest, RaycastNearestLayerMaskAndInactive) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  const nlohmann::json down = {
      {"mode", "raycast"}, {"origin", {0, 100, 0}}, {"direction", {0, -1, 0}}};
  ToolResult ray = fixture.CallTool("query_scene_spatial", down);
  ASSERT_TRUE(ray.ok) << ray.result.dump();
  const auto& hits = ray.result["hits"];
  ASSERT_GT(hits.size(), 1u);
  // Player (sphere collider, top near y=1) is closer than Ground (top y=0).
  EXPECT_EQ(hits[0]["name"], "Player");
  EXPECT_EQ(hits[0]["shape"], "bounds");
  EXPECT_TRUE(hits[0].contains("distance"));
  EXPECT_TRUE(hits[0].contains("point"));
  EXPECT_GT(hits[0]["distance"].get<float>(), 0.0f);
  for (const auto& hit : hits) EXPECT_EQ(hit["shape"], "bounds");

  MapNode* player = scene.GetMap().FindNode("Player");
  ASSERT_NE(player, nullptr);
  const kEntityId entity = scene.GetEntityForNode(player->uid);
  scene.SetLayer(entity, 3);
  nlohmann::json layer_zero = down;
  layer_zero["layer_mask"] = 1;
  ToolResult without_player =
      fixture.CallTool("query_scene_spatial", layer_zero);
  ASSERT_TRUE(without_player.ok) << without_player.result.dump();
  EXPECT_FALSE(HasHit(without_player, "Player"));
  EXPECT_TRUE(HasHit(without_player, "Ground"));
  nlohmann::json layer_three = down;
  layer_three["layer_mask"] = 1u << 3;
  ToolResult with_player = fixture.CallTool("query_scene_spatial", layer_three);
  ASSERT_TRUE(with_player.ok) << with_player.result.dump();
  EXPECT_TRUE(HasHit(with_player, "Player"));
  EXPECT_FALSE(HasHit(with_player, "Ground"));

  auto* info = scene.Objects().GetProperty<NodeInfo>(entity);
  ASSERT_NE(info, nullptr);
  info->active = false;
  info->active_in_hierarchy = false;
  ToolResult inactive = fixture.CallTool("query_scene_spatial", down);
  ASSERT_TRUE(inactive.ok) << inactive.result.dump();
  EXPECT_FALSE(HasHit(inactive, "Player"));
  EXPECT_TRUE(HasHit(inactive, "Ground"));
}

TEST(McpToolsSpatialCaptureTest, RaycastGroundDefaultsToStraightDown) {
  McpToolFixture fixture;
  ToolResult ground =
      fixture.CallTool("query_scene_spatial",
                       {{"mode", "raycast_ground"}, {"origin", {0, 100, 0}}});
  ASSERT_TRUE(ground.ok) << ground.result.dump();
  ASSERT_GT(ground.result["hits"].size(), 0u);
  EXPECT_EQ(ground.result["hits"][0]["name"], "Player");
  EXPECT_EQ(ground.result["hits"][0]["shape"], "bounds");

  // max_distance shorter than the first hit yields no hits.
  ToolResult short_ray =
      fixture.CallTool("query_scene_spatial", {{"mode", "raycast_ground"},
                                               {"origin", {0, 100, 0}},
                                               {"max_distance", 1.0}});
  ASSERT_TRUE(short_ray.ok) << short_ray.result.dump();
  EXPECT_EQ(short_ray.result["count"], 0u);
  EXPECT_TRUE(short_ray.result["hit"].is_null());
}

TEST(McpToolsSpatialCaptureTest, ViewStateSaveRestoreOnErrorAndNoHost) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  EXPECT_EQ(ErrorText(fixture.CallTool("capture_viewport")),
            "no render host available");

  EditorScene::ViewState before;
  before.view_2d = false;
  before.target = glm::vec3(1.0f, 2.0f, 3.0f);
  before.distance = 50.0f;
  before.yaw = 10.0f;
  before.pitch = -5.0f;
  before.viewport_size = glm::vec2(64.0f, 64.0f);
  scene.SetViewState(before);

  FakeRenderHost host;
  host.fail = true;
  scene.SetRenderHost(&host);
  ToolResult failed = fixture.CallTool(
      "capture_viewport",
      {{"preset", "isometric"}, {"width", 64}, {"height", 64}});
  EXPECT_FALSE(failed.ok);
  EXPECT_NE(ErrorText(failed).find("copy scene pixels"), std::string::npos);

  const EditorScene::ViewState after = scene.GetViewState();
  EXPECT_EQ(after.view_2d, before.view_2d);
  EXPECT_EQ(after.target, before.target);
  EXPECT_EQ(after.distance, before.distance);
  EXPECT_EQ(after.yaw, before.yaw);
  EXPECT_EQ(after.pitch, before.pitch);
  EXPECT_EQ(after.viewport_size, before.viewport_size);

  // An invalid preset is rejected before the camera is changed.
  ToolResult invalid =
      fixture.CallTool("capture_viewport", {{"preset", "sideways"}});
  EXPECT_FALSE(invalid.ok);
  EXPECT_NE(ErrorText(invalid).find("unknown capture_viewport preset"),
            std::string::npos);
  EXPECT_EQ(scene.GetViewState().target, before.target);
  scene.SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, CapturesPngWithSoftwareOverlays) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  FakeRenderHost host;
  scene.SetRenderHost(&host);
  const EditorScene::ViewState before = scene.GetViewState();

  const nlohmann::json args = {
      {"preset", "top_down"},
      {"frame", {{"path", "Player"}}},
      {"width", 64},
      {"height", 64},
      {"colliders", true},
      {"highlight", nlohmann::json::array({{{"path", "Player"}}})},
  };
  ToolResult result = fixture.CallTool("capture_viewport", args);
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["format"], "png");
  EXPECT_EQ(result.result["width"], 64);
  EXPECT_EQ(result.result["height"], 64);
  ASSERT_EQ(result.images.size(), 1u);
  EXPECT_EQ(result.images[0].label, "viewport.png");

  const auto& png = result.images[0].data;
  const std::array<uint8_t, 8> signature = {137, 80, 78, 71, 13, 10, 26, 10};
  ASSERT_GE(png.size(), signature.size());
  EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png.begin()));

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(
      png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
  ASSERT_NE(pixels, nullptr) << "the returned PNG must decode";
  EXPECT_EQ(width, 64);
  EXPECT_EQ(height, 64);
  int changed = 0;
  for (size_t i = 0; i < static_cast<size_t>(width) * height * 4; i += 4) {
    if (pixels[i] != 10 || pixels[i + 1] != 20 || pixels[i + 2] != 30)
      changed++;
  }
  stbi_image_free(pixels);
  EXPECT_GT(changed, 0) << "collider/highlight overlays must draw into the PNG";

  // The preset changed the camera, but the guard put the original one back.
  const EditorScene::ViewState after = scene.GetViewState();
  EXPECT_EQ(after.view_2d, before.view_2d);
  EXPECT_EQ(after.target, before.target);
  EXPECT_EQ(after.distance, before.distance);
  EXPECT_EQ(after.yaw, before.yaw);
  EXPECT_EQ(after.pitch, before.pitch);
  scene.SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, RaycastReportsWorldFaceNormals) {
  McpToolFixture fixture;
  ToolResult down =
      fixture.CallTool("query_scene_spatial", {{"mode", "raycast"},
                                               {"origin", {0, 100, 0}},
                                               {"direction", {0, -1, 0}}});
  ASSERT_TRUE(down.ok) << down.result.dump();
  ASSERT_GT(down.result["hits"].size(), 0u);
  const auto& top = down.result["hits"][0]["normal"];
  ASSERT_EQ(top.size(), 3u);
  EXPECT_NEAR(top[0].get<float>(), 0.0f, 1e-3f);
  EXPECT_NEAR(top[1].get<float>(), 1.0f, 1e-3f);  // floor's upward face
  EXPECT_NEAR(top[2].get<float>(), 0.0f, 1e-3f);

  // A sideways ray into the Ground box should report an X-facing normal.
  ToolResult side =
      fixture.CallTool("query_scene_spatial", {{"mode", "raycast"},
                                               {"origin", {30, -0.5f, 0}},
                                               {"direction", {-1, 0, 0}}});
  ASSERT_TRUE(side.ok) << side.result.dump();
  ASSERT_GT(side.result["hits"].size(), 0u);
  const auto& face = side.result["hits"][0]["normal"];
  ASSERT_EQ(face.size(), 3u);
  EXPECT_NEAR(face[0].get<float>(), 1.0f, 1e-3f);
  EXPECT_NEAR(face[1].get<float>(), 0.0f, 1e-3f);
  EXPECT_NEAR(face[2].get<float>(), 0.0f, 1e-3f);
}

TEST(McpToolsSpatialCaptureTest, CaptureSuggestsNearMissNodes) {
  McpToolFixture fixture;
  FakeRenderHost host;
  fixture.Scene().SetRenderHost(&host);

  const nlohmann::json bad_ref = {{"path", "RollABall/Plyer"}};
  ToolResult highlight = fixture.CallTool(
      "capture_viewport",
      {{"preset", "current"}, {"highlight", nlohmann::json::array({bad_ref})}});
  EXPECT_FALSE(highlight.ok);
  EXPECT_EQ(highlight.result["suggestion"], "RollABall/Player");

  ToolResult frame = fixture.CallTool(
      "capture_viewport", {{"preset", "current"}, {"frame", bad_ref}});
  EXPECT_FALSE(frame.ok);
  EXPECT_EQ(frame.result["suggestion"], "RollABall/Player");
  fixture.Scene().SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, TopDownPresetLooksDownAndKeepsTarget) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  FakeRenderHost host;
  scene.SetRenderHost(&host);

  EditorScene::ViewState view = scene.GetViewState();
  view.target = glm::vec3(3.0f, 7.0f, -2.0f);
  view.distance = 40.0f;
  scene.SetViewState(view);

  ToolResult result =
      fixture.CallTool("capture_viewport",
                       {{"preset", "top_down"}, {"width", 64}, {"height", 64}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  const auto& camera = result.result["camera"];
  ASSERT_TRUE(result.result.contains("camera"));
  EXPECT_NEAR(camera["target"][0].get<float>(), 3.0f, 1e-3f);
  EXPECT_NEAR(camera["target"][1].get<float>(), 7.0f, 1e-3f);
  EXPECT_NEAR(camera["target"][2].get<float>(), -2.0f, 1e-3f);
  EXPECT_GT(camera["position"][1].get<float>(), 7.0f)
      << "top_down must look down -Y from above the target";
  EXPECT_NEAR(camera["fov"].get<float>(), 60.0f, 1e-3f);

  // Original view restored, including its 3D/2D flag.
  EXPECT_EQ(scene.GetViewState().target, view.target);
  EXPECT_EQ(scene.GetViewState().view_2d, view.view_2d);
  scene.SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, PlayerPovUsesEnabledCameraAndReportsIt) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  FakeRenderHost host;
  scene.SetRenderHost(&host);

  ToolResult result = fixture.CallTool(
      "capture_viewport",
      {{"preset", "player_pov"}, {"width", 64}, {"height", 64}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  ASSERT_TRUE(result.result.contains("camera"));
  // The map's Camera node is at (0, 10, 10); the orbit target is derived from
  // it, so the restored capture position lands back on that node.
  const auto& position = result.result["camera"]["position"];
  EXPECT_NEAR(position[0].get<float>(), 0.0f, 0.5f);
  EXPECT_NEAR(position[1].get<float>(), 10.0f, 0.5f);
  EXPECT_NEAR(position[2].get<float>(), 10.0f, 0.5f);

  MapNode* camera_node = scene.GetMap().FindNode("Camera");
  ASSERT_NE(camera_node, nullptr);
  auto* settings = scene.Objects().GetProperty<CameraComponent>(
      scene.GetEntityForNode(camera_node->uid));
  ASSERT_NE(settings, nullptr);
  settings->enabled = false;
  ToolResult disabled =
      fixture.CallTool("capture_viewport", {{"preset", "player_pov"}});
  EXPECT_FALSE(disabled.ok);
  EXPECT_EQ(ErrorText(disabled), "no enabled Camera component in the scene");
  scene.SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, OverlayUsesNodeWorldTransform) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  FakeRenderHost host;
  scene.SetRenderHost(&host);

  // Look straight at an off-origin node (Walls/East at x=10.25) so its
  // projected box belongs near the centre, while the world origin is far to
  // the left. If DrawBox forgot the model matrix, the overlay would sit at the
  // origin's projection instead.
  EditorScene::ViewState view = scene.GetViewState();
  view.view_2d = false;
  view.target = glm::vec3(10.25f, 0.5f, 0.0f);
  view.yaw = 0.0f;
  view.pitch = 0.0f;
  view.distance = 50.0f;
  view.viewport_size = glm::vec2(128.0f, 128.0f);
  scene.SetViewState(view);

  const glm::mat4 projection = scene.GetSceneViewProjection();
  auto project = [&](const glm::vec3& world) {
    const glm::vec4 clip = projection * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2((ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f) * 128.0f;
  };
  const glm::vec2 expected = project(glm::vec3(10.25f, 0.5f, 0.0f));
  const glm::vec2 origin = project(glm::vec3(0.0f));

  ToolResult result = fixture.CallTool(
      "capture_viewport",
      {{"preset", "current"},
       {"width", 128},
       {"height", 128},
       {"highlight", nlohmann::json::array({{{"path", "Walls/East"}}})}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  ASSERT_EQ(result.images.size(), 1u);
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels =
      stbi_load_from_memory(result.images[0].data.data(),
                            static_cast<int>(result.images[0].data.size()),
                            &width, &height, &channels, 4);
  ASSERT_NE(pixels, nullptr);
  double sx = 0.0;
  double sy = 0.0;
  int changed = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const size_t i = (static_cast<size_t>(y) * width + x) * 4;
      if (pixels[i] != 10 || pixels[i + 1] != 20 || pixels[i + 2] != 30) {
        sx += x;
        sy += y;
        changed++;
      }
    }
  }
  stbi_image_free(pixels);
  ASSERT_GT(changed, 0) << "the highlight box must draw into the PNG";
  const glm::vec2 centroid(static_cast<float>(sx / changed),
                           static_cast<float>(sy / changed));
  EXPECT_LT(glm::distance(centroid, expected), 16.0f)
      << "centroid " << centroid.x << "," << centroid.y << " expected "
      << expected.x << "," << expected.y;
  EXPECT_GT(glm::distance(centroid, origin), 16.0f)
      << "the box must not be drawn at the world origin";
  scene.SetRenderHost(nullptr);
}

TEST(McpToolsSpatialCaptureTest, LifetimeTokenExpiresOnDestroyScene) {
  McpToolFixture fixture;
  std::weak_ptr<const void> token = fixture.Scene().LifetimeToken();
  ASSERT_FALSE(token.expired());
  fixture.DestroyScene();
  EXPECT_TRUE(token.expired());
}

TEST(McpToolsSpatialCaptureTest, HeadlessFilamentCapture) {
  McpToolFixture fixture;
  std::filesystem::path mproj;
  for (const auto& entry :
       std::filesystem::directory_iterator(fixture.Root())) {
    if (entry.path().extension() == ".mproj") mproj = entry.path();
  }
  ASSERT_FALSE(mproj.empty());

  auto app = std::make_unique<DefaultApplication>("MCP capture test", 128, 128);
  app->EnableHeadlessMode(true);
  auto scene = std::make_unique<EditorScene>(mproj);
  app->SetGameScene(std::move(scene));
  if (!app->StartHosted()) {
    GTEST_SKIP() << "Vulkan/Filament could not initialize headlessly here";
  }
  app->EnableSceneTexture(true);
  app->SetSceneViewportSize(128, 128);

  int width = 0;
  int height = 0;
  uint64_t frame_id = 0;
  std::vector<uint8_t> pixels;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  bool ready = false;
  while (std::chrono::steady_clock::now() < deadline) {
    app->StepFrame();
    if (app->CopyScenePixels(width, height, pixels, &frame_id) &&
        frame_id > 0) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!ready) {
    GTEST_SKIP() << "the headless renderer produced no frames here";
  }

  // The fixture's scene borrows the app's render host for one capture. The
  // camera preset is `current`, so no resize is needed for this frame.
  fixture.Scene().SetRenderHost(app.get());
  ToolResult result = fixture.CallTool(
      "capture_viewport",
      {{"preset", "current"}, {"width", 128}, {"height", 128}});
  EXPECT_TRUE(result.ok) << result.result.dump();
  if (result.ok) {
    ASSERT_EQ(result.images.size(), 1u);
    EXPECT_GE(result.images[0].data.size(), 8u);
    EXPECT_EQ(result.images[0].data[0], 137);
  }
  fixture.Scene().SetRenderHost(nullptr);
}
