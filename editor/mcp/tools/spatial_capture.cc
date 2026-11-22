// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * spatial_capture.cc
 *
 * The spatial-query and viewport-capture MCP tools. Both are Read mode.
 *
 * There is no Jolt physics world in edit mode, so bounds and rays come from
 * the map's node components. A ray is tested against each node's ORIENTED
 * bounding box with a slab test in the node's local space - the same box and
 * the same math editor picking uses. It never tests triangles or colliders
 * simulated by physics.
 *
 * The capture borrows the editor camera: it snapshots EditorScene::ViewState,
 * sets up the requested view, copies the offscreen RGBA frame, draws software
 * overlays, encodes a PNG and restores the snapshot through an RAII guard on
 * every exit path, including errors.
 */

#include <wx/app.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <stb/stb_image_write.h>

#include "components/components.h"
#include "editor_scene.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/scene_json.h"
#include "properties/render_mesh.h"
#include "properties/transform.h"

namespace Mercury {

namespace {

using mcp::FullNodePath;
using mcp::Png;
using mcp::ToolDone;
using mcp::ToolResult;
using nlohmann::json;

constexpr float kFovDegrees = 60.0f;
constexpr float kDefaultRayDistance = 10000.0f;
constexpr uint32_t kAllLayers = ~0u;
// Upper bound on UI turns spent waiting for a frame with the new camera.
constexpr int kMaxCaptureFrames = 600;

json Error(std::string message, const std::string& suggestion = {}) {
  json out{{"error", std::move(message)}};
  if (!suggestion.empty()) out["suggestion"] = suggestion;
  return out;
}

json Vec3Json(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

bool ParseVec3(const json& value, glm::vec3* out, std::string* error,
               const char* name) {
  if (!value.is_array() || value.size() != 3) {
    *error = std::string("\"") + name + "\" must be [x, y, z]";
    return false;
  }
  glm::vec3 result;
  for (int i = 0; i < 3; i++) {
    if (!value[i].is_number()) {
      *error = std::string("\"") + name + "\" must contain numbers";
      return false;
    }
    result[i] = value[i].get<float>();
    if (!std::isfinite(result[i])) {
      *error = std::string("\"") + name + "\" must contain finite numbers";
      return false;
    }
  }
  *out = result;
  return true;
}

bool ParseDimension(const json& args, const char* key, int fallback, int* out,
                    std::string* error) {
  if (!args.contains(key) || args[key].is_null()) {
    *out = fallback;
    return true;
  }
  const json& value = args[key];
  if (!value.is_number_integer() || value.get<int64_t>() <= 0) {
    *error = std::string("\"") + key + "\" must be a positive integer";
    return false;
  }
  *out = static_cast<int>(std::min<int64_t>(value.get<int64_t>(), 100000));
  return true;
}

bool ParseLayerMask(const json& args, uint32_t* out, std::string* error) {
  if (!args.contains("layer_mask") || args["layer_mask"].is_null()) {
    *out = kAllLayers;
    return true;
  }
  const json& value = args["layer_mask"];
  if (!value.is_number_integer()) {
    *error = "\"layer_mask\" must be a non-negative integer";
    return false;
  }
  const int64_t mask = value.get<int64_t>();
  if (mask < 0) {
    *error = "\"layer_mask\" must be a non-negative integer";
    return false;
  }
  *out = static_cast<uint32_t>(mask);
  return true;
}

// A node is raycastable only when it is active and on a selected layer.
bool NodeVisible(const NodeInfo* info, uint32_t layer_mask) {
  if (info && (!info->active || !info->active_in_hierarchy)) return false;
  const int layer = info ? info->layer : 0;
  return (layer_mask & (1u << (layer & 31))) != 0;
}

json NodeSummary(const MapNode& node) {
  return json{
      {"uid", node.uid}, {"name", node.name}, {"path", FullNodePath(node)}};
}

json BoundsJson(const EditorScene::NodeWorldBounds& bounds) {
  json out{
      {"world_min", Vec3Json(bounds.world_min)},
      {"world_max", Vec3Json(bounds.world_max)},
      {"local_min", Vec3Json(bounds.local_min)},
      {"local_max", Vec3Json(bounds.local_max)},
  };
  // glm matrices are column-major; flatten them the same way clients read
  // glm::value_ptr so the 16 numbers can be rebuilt directly.
  json matrix = json::array();
  for (int column = 0; column < 4; column++) {
    for (int row = 0; row < 4; row++)
      matrix.push_back(bounds.model[column][row]);
  }
  out["model"] = matrix;
  return out;
}

struct RayBoxHit {
  float t = 0.0f;
  // Local outward face normal at the entry point: one axis set to +/-1.
  glm::vec3 normal = glm::vec3(0.0f);
};

std::optional<RayBoxHit> RayBox(glm::vec3 origin, glm::vec3 direction,
                                glm::vec3 min, glm::vec3 max) {
  float t_min = -std::numeric_limits<float>::max();
  float t_max = std::numeric_limits<float>::max();
  glm::vec3 normal(0.0f);
  for (int i = 0; i < 3; i++) {
    if (std::abs(direction[i]) < 1e-9f) {
      if (origin[i] < min[i] || origin[i] > max[i]) return std::nullopt;
      continue;
    }
    float t_near = (min[i] - origin[i]) / direction[i];
    float t_far = (max[i] - origin[i]) / direction[i];
    // A positive direction enters through min (outward normal -axis); a
    // negative one enters through max (+axis).
    float face = -1.0f;
    if (t_near > t_far) {
      std::swap(t_near, t_far);
      face = 1.0f;
    }
    if (t_near > t_min) {
      t_min = t_near;
      normal = glm::vec3(0.0f);
      normal[i] = face;
    }
    t_max = std::min(t_max, t_far);
    if (t_min > t_max) return std::nullopt;
  }
  // An origin inside the box enters at t = 0; report the last crossed face
  // as the normal rather than a zero vector.
  return RayBoxHit{std::max(t_min, 0.0f), normal};
}

// ------------------------------------------------------- query_scene_spatial -

ToolResult QueryBounds(EditorScene& scene, const json& args) {
  if (!args.contains("node")) {
    return ToolResult{
        false, Error("query_scene_spatial mode \"bounds\" requires \"node\"")};
  }
  std::string error;
  std::string suggestion;
  MapNode* node = mcp::ResolveNode(scene, args["node"], &error, &suggestion);
  if (!node) return ToolResult{false, Error(error, suggestion)};
  EditorScene::NodeWorldBounds bounds;
  if (!scene.GetNodeWorldBounds(*node, bounds)) {
    return ToolResult{
        false, Error("node has no bounds to query: " + FullNodePath(*node))};
  }
  json out = BoundsJson(bounds);
  out["node"] = NodeSummary(*node);
  return ToolResult{true, out, {}};
}

ToolResult QueryNodesInBox(EditorScene& scene, const json& args) {
  if (!args.contains("box")) {
    return ToolResult{
        false,
        Error("query_scene_spatial mode \"nodes_in_box\" requires \"box\"")};
  }
  const json& box = args["box"];
  if (!box.is_object() || !box.contains("min") || !box.contains("max")) {
    return ToolResult{
        false, Error("\"box\" must be {\"min\": [x,y,z], \"max\": [x,y,z]}")};
  }
  glm::vec3 min, max;
  std::string error;
  if (!ParseVec3(box["min"], &min, &error, "box.min") ||
      !ParseVec3(box["max"], &max, &error, "box.max")) {
    return ToolResult{false, Error(error)};
  }
  if (glm::any(glm::lessThan(max, min))) {
    return ToolResult{false,
                      Error("\"box.max\" must not be less than \"box.min\"")};
  }
  uint32_t layer_mask = kAllLayers;
  if (!ParseLayerMask(args, &layer_mask, &error)) {
    return ToolResult{false, Error(error)};
  }

  json nodes = json::array();
  scene.GetMap().Visit([&](MapNode& node) {
    if (node.IsRoot()) return;
    const kEntityId entity = scene.GetEntityForNode(node.uid);
    const NodeInfo* info = scene.Objects().GetProperty<NodeInfo>(entity);
    if (!NodeVisible(info, layer_mask)) return;
    EditorScene::NodeWorldBounds bounds;
    if (!scene.GetNodeWorldBounds(entity, bounds)) return;
    if (glm::any(glm::lessThan(bounds.world_max, min)) ||
        glm::any(glm::lessThan(max, bounds.world_min))) {
      return;
    }
    json item = NodeSummary(node);
    item["world_min"] = Vec3Json(bounds.world_min);
    item["world_max"] = Vec3Json(bounds.world_max);
    nodes.push_back(std::move(item));
  });
  return ToolResult{true, json{{"nodes", nodes}, {"count", nodes.size()}}, {}};
}

ToolResult QueryRaycast(EditorScene& scene, const json& args, bool ground) {
  std::string error;
  glm::vec3 origin(0.0f);
  if (args.contains("origin")) {
    if (!ParseVec3(args["origin"], &origin, &error, "origin")) {
      return ToolResult{false, Error(error)};
    }
  } else if (ground) {
    origin = glm::vec3(0.0f, 1000.0f, 0.0f);
  } else {
    return ToolResult{false, Error("raycast requires \"origin\": [x, y, z]")};
  }

  glm::vec3 direction(0.0f, -1.0f, 0.0f);
  if (args.contains("direction")) {
    if (!ParseVec3(args["direction"], &direction, &error, "direction")) {
      return ToolResult{false, Error(error)};
    }
  }
  if (glm::length(direction) < 1e-6f) {
    return ToolResult{false, Error("\"direction\" must not be zero")};
  }
  direction = glm::normalize(direction);

  float max_distance = kDefaultRayDistance;
  if (args.contains("max_distance")) {
    if (!args["max_distance"].is_number() ||
        !std::isfinite(args["max_distance"].get<float>())) {
      return ToolResult{false,
                        Error("\"max_distance\" must be a finite number")};
    }
    max_distance = args["max_distance"].get<float>();
    if (max_distance < 0.0f) {
      return ToolResult{false, Error("\"max_distance\" must not be negative")};
    }
  }
  uint32_t layer_mask = kAllLayers;
  if (!ParseLayerMask(args, &layer_mask, &error)) {
    return ToolResult{false, Error(error)};
  }

  struct Hit {
    float distance = 0.0f;
    json value;
  };
  std::vector<Hit> hits;
  scene.GetMap().Visit([&](MapNode& node) {
    if (node.IsRoot()) return;
    const kEntityId entity = scene.GetEntityForNode(node.uid);
    const NodeInfo* info = scene.Objects().GetProperty<NodeInfo>(entity);
    if (!NodeVisible(info, layer_mask)) return;
    EditorScene::NodeWorldBounds bounds;
    if (!scene.GetNodeWorldBounds(entity, bounds)) return;

    glm::vec3 min = bounds.local_min;
    glm::vec3 max = bounds.local_max;
    // Match PickNode: give flat bounds a hair of thickness so grid-like nodes
    // can still be hit.
    for (int i = 0; i < 3; i++) {
      if (max[i] - min[i] < 1e-4f) {
        min[i] -= 1e-3f;
        max[i] += 1e-3f;
      }
    }
    const glm::mat4 inverse = glm::inverse(bounds.model);
    const glm::vec3 local_origin(inverse * glm::vec4(origin, 1.0f));
    const glm::vec3 local_direction(inverse * glm::vec4(direction, 0.0f));
    const auto hit = RayBox(local_origin, local_direction, min, max);
    if (!hit || hit->t < 0.0f || hit->t > max_distance) return;

    // The slab test's local face normal becomes a world normal with the
    // inverse-transpose of the node's model matrix (correct under non-uniform
    // scale and rotation).
    const glm::mat3 normal_matrix =
        glm::transpose(glm::inverse(glm::mat3(bounds.model)));
    const glm::vec3 world_normal = glm::normalize(normal_matrix * hit->normal);

    const glm::vec3 point = origin + direction * hit->t;
    json value = NodeSummary(node);
    value["distance"] = hit->t;
    value["point"] = Vec3Json(point);
    value["normal"] = Vec3Json(world_normal);
    value["shape"] = "bounds";
    if (info) value["layer"] = info->layer;
    hits.push_back({hit->t, std::move(value)});
  });
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    return a.distance < b.distance;
  });

  json list = json::array();
  for (Hit& hit : hits) list.push_back(std::move(hit.value));
  json out{{"hits", list},
           {"count", list.size()},
           {"hit", list.empty() ? json(nullptr) : list.front()},
           {"shape", "bounds"}};
  return ToolResult{true, out, {}};
}

ToolResult QuerySceneSpatial(EditorScene& scene, const json& args) {
  const std::string mode = args.contains("mode") && args["mode"].is_string()
                               ? args["mode"].get<std::string>()
                               : std::string();
  if (mode == "bounds") return QueryBounds(scene, args);
  if (mode == "nodes_in_box") return QueryNodesInBox(scene, args);
  if (mode == "raycast") return QueryRaycast(scene, args, false);
  if (mode == "raycast_ground") return QueryRaycast(scene, args, true);
  return ToolResult{
      false,
      Error("unknown query_scene_spatial mode \"" + mode +
            "\": expected bounds, nodes_in_box, raycast or raycast_ground")};
}

// ----------------------------------------------------------- capture helper -

void SetPixel(std::vector<uint8_t>& rgba, int width, int height, int x, int y,
              const std::array<uint8_t, 4>& color) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  const size_t index = (static_cast<size_t>(y) * width + x) * 4;
  rgba[index + 0] = color[0];
  rgba[index + 1] = color[1];
  rgba[index + 2] = color[2];
  rgba[index + 3] = color[3];
}

// Liang-Barsky clip in pixel space, so an edge that crosses the viewport is
// drawn up to its border instead of being dropped because an endpoint is
// outside it.
bool ClipLine(glm::vec2& a, glm::vec2& b, int width, int height) {
  const float x_max = static_cast<float>(width - 1);
  const float y_max = static_cast<float>(height - 1);
  const glm::vec2 delta = b - a;
  const float p[4] = {-delta.x, delta.x, -delta.y, delta.y};
  const float q[4] = {a.x, x_max - a.x, a.y, y_max - a.y};
  float t0 = 0.0f;
  float t1 = 1.0f;
  for (int i = 0; i < 4; i++) {
    if (std::abs(p[i]) < 1e-9f) {
      if (q[i] < 0.0f) return false;  // parallel to and outside the border
      continue;
    }
    const float r = q[i] / p[i];
    if (p[i] < 0.0f) {
      t0 = std::max(t0, r);
    } else {
      t1 = std::min(t1, r);
    }
    if (t0 > t1) return false;
  }
  const glm::vec2 from = a;
  a = from + delta * t0;
  b = from + delta * t1;
  return true;
}

// Small software line drawing (DDA with a 2x2 pen) for the overlay boxes. The
// captured image is RGBA8, so no wx/Filament drawing state is involved.
void DrawLine(std::vector<uint8_t>& rgba, int width, int height, glm::vec2 a,
              glm::vec2 b, const std::array<uint8_t, 4>& color) {
  if (!ClipLine(a, b, width, height)) return;
  const glm::vec2 delta = b - a;
  const int steps = std::max(1, static_cast<int>(std::ceil(glm::max(
                                    std::abs(delta.x), std::abs(delta.y)))));
  for (int i = 0; i <= steps; i++) {
    const float t = static_cast<float>(i) / steps;
    const int x = static_cast<int>(std::lround(a.x + delta.x * t));
    const int y = static_cast<int>(std::lround(a.y + delta.y * t));
    SetPixel(rgba, width, height, x, y, color);
    SetPixel(rgba, width, height, x + 1, y, color);
    SetPixel(rgba, width, height, x, y + 1, color);
    SetPixel(rgba, width, height, x + 1, y + 1, color);
  }
}

constexpr int kBoxEdges[12][2] = {
    {0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
    {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

void DrawBox(std::vector<uint8_t>& rgba, int width, int height,
             const glm::mat4& view_projection,
             const EditorScene::NodeWorldBounds& bounds,
             const std::array<uint8_t, 4>& color) {
  glm::vec2 pixel[8];
  bool visible[8] = {};
  for (int i = 0; i < 8; i++) {
    const glm::vec3 corner(i & 1 ? bounds.local_max.x : bounds.local_min.x,
                           i & 2 ? bounds.local_max.y : bounds.local_min.y,
                           i & 4 ? bounds.local_max.z : bounds.local_min.z);
    // The box is in the node's local space; the model matrix places it in
    // the world before the camera projection (as it is for picking).
    const glm::vec4 world = bounds.model * glm::vec4(corner, 1.0f);
    const glm::vec4 clip = view_projection * world;
    if (clip.w <= 1e-5f) continue;  // behind the camera
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    pixel[i] = glm::vec2((ndc.x + 1.0f) * 0.5f * width,
                         (1.0f - ndc.y) * 0.5f * height);
    visible[i] = true;
  }
  for (const auto& edge : kBoxEdges) {
    if (visible[edge[0]] && visible[edge[1]]) {
      DrawLine(rgba, width, height, pixel[edge[0]], pixel[edge[1]], color);
    }
  }
}

bool HasCollider(PObjectManager& objects, kEntityId entity) {
  return objects.GetProperty<BoxCollider>(entity) ||
         objects.GetProperty<SphereCollider>(entity) ||
         objects.GetProperty<CapsuleCollider>(entity) ||
         objects.GetProperty<CylinderCollider>(entity) ||
         objects.GetProperty<MeshCollider>(entity);
}

// The capture plan owns the camera snapshot. Its destructor restores the view
// on EVERY exit path: an error, a missing render host, a failed PNG encode or
// a successful capture.
struct CapturePlan {
  explicit CapturePlan(EditorScene& scene) : scene(scene) {}
  CapturePlan(const CapturePlan&) = delete;
  CapturePlan& operator=(const CapturePlan&) = delete;
  ~CapturePlan() {
    // A deferred capture may outlive the editor scene. Never touch a dead
    // scene, not even to restore the camera.
    if (camera_changed && !lifetime.expired()) scene.SetViewState(saved);
  }

  EditorScene& scene;
  // Expires when the scene starts shutting down; see
  // EditorScene::LifetimeToken.
  std::weak_ptr<const void> lifetime;
  EditorScene::ViewState saved;
  EditorScene::ViewState desired;
  std::string preset = "current";
  std::vector<kEntityId> highlights;
  bool colliders = false;
  bool camera_changed = false;
  uint64_t saved_frame = 0;

  ToolResult Capture() {
    RenderHost* host = scene.GetRenderHost();
    if (!host) return ToolResult{false, Error("no render host available")};
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
    std::vector<uint8_t> rgba;
    if (!host->CopyScenePixels(width, height, rgba, &frame_id) || width <= 0 ||
        height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4) {
      return ToolResult{
          false, Error("could not copy scene pixels from the render host")};
    }

    const glm::mat4 view_projection = scene.GetSceneViewProjection();
    constexpr std::array<uint8_t, 4> kColliderColor = {60, 220, 120, 255};
    constexpr std::array<uint8_t, 4> kHighlightColor = {255, 215, 50, 255};
    if (colliders) {
      scene.GetMap().Visit([&](MapNode& node) {
        if (node.IsRoot()) return;
        const kEntityId entity = scene.GetEntityForNode(node.uid);
        if (!HasCollider(scene.Objects(), entity)) return;
        EditorScene::NodeWorldBounds bounds;
        if (scene.GetNodeWorldBounds(entity, bounds)) {
          DrawBox(rgba, width, height, view_projection, bounds, kColliderColor);
        }
      });
    }
    for (kEntityId entity : highlights) {
      EditorScene::NodeWorldBounds bounds;
      if (scene.GetNodeWorldBounds(entity, bounds)) {
        DrawBox(rgba, width, height, view_projection, bounds, kHighlightColor);
      }
    }

    std::vector<uint8_t> png_bytes;
    png_bytes.reserve(static_cast<size_t>(width) * height);
    if (!stbi_write_png_to_func(
            [](void* context, void* data, int size) {
              auto* bytes = static_cast<std::vector<uint8_t>*>(context);
              const auto* chunk = static_cast<const uint8_t*>(data);
              bytes->insert(bytes->end(), chunk, chunk + size);
            },
            &png_bytes, width, height, 4, rgba.data(), width * 4) ||
        png_bytes.empty()) {
      return ToolResult{false,
                        Error("failed to encode the captured image as PNG")};
    }

    json out{{"width", width},
             {"height", height},
             {"preset", preset},
             {"frame_id", frame_id},
             {"colliders", colliders},
             {"highlight_count", highlights.size()},
             {"format", "png"},
             {"camera",
              {{"position", Vec3Json(scene.GetCameraPosition())},
               {"target", Vec3Json(scene.GetCameraTarget())},
               {"fov", kFovDegrees}}}};
    return ToolResult{true, out, {Png{std::move(png_bytes), "viewport.png"}}};
  }
};

bool ParsePreset(const json& args, CapturePlan* plan, std::string* error,
                 std::string* suggestion) {
  if (suggestion) suggestion->clear();
  const std::string preset =
      args.contains("preset") && args["preset"].is_string()
          ? args["preset"].get<std::string>()
          : std::string("current");
  if (preset != "current" && preset != "top_down" && preset != "isometric" &&
      preset != "front" && preset != "player_pov") {
    *error = "unknown capture_viewport preset \"" + preset +
             "\": expected current, top_down, isometric, front or player_pov";
    return false;
  }
  plan->preset = preset;
  EditorScene::ViewState desired = plan->saved;

  if (preset == "top_down") {
    // view_2d looks along -Z (an XY side view), not down. A near-vertical 3D
    // camera pitch is the real top-down view.
    desired.view_2d = false;
    desired.yaw = 0.0f;
    desired.pitch = -89.9f;
  } else if (preset == "isometric") {
    desired.view_2d = false;
    desired.yaw = 35.0f;
    desired.pitch = -25.0f;
  } else if (preset == "front") {
    desired.view_2d = false;
    desired.yaw = 0.0f;
    desired.pitch = 0.0f;
  } else if (preset == "player_pov") {
    desired.view_2d = false;
    // The map's Camera component is mirrored into a CameraComponent property;
    // `enabled` (not the editor's active-camera id) decides whether it draws.
    kEntityId camera = entt::null;
    plan->scene.GetMap().Visit([&](MapNode& node) {
      if (camera != entt::null || node.IsRoot()) return;
      const kEntityId entity = plan->scene.GetEntityForNode(node.uid);
      auto* settings =
          plan->scene.Objects().GetProperty<CameraComponent>(entity);
      if (settings && settings->enabled &&
          plan->scene.Objects().GetProperty<Transform>(entity)) {
        camera = entity;
      }
    });
    if (camera == entt::null) {
      *error = "no enabled Camera component in the scene";
      return false;
    }
    auto* transform = plan->scene.Objects().GetProperty<Transform>(camera);
    const glm::vec3 position = transform->GetPosition();
    const glm::vec3 rotation = transform->GetRotate();  // pitch, yaw, roll
    const glm::vec3 forward =
        Transform::EulerToQuat(rotation) * glm::vec3(0.0f, 0.0f, -1.0f);
    desired.yaw = rotation.y;
    desired.pitch = rotation.x;
    const float distance = std::max(desired.distance, 0.1f);
    desired.target = position + forward * distance;
  }

  // `frame` overrides the preset's framing but keeps its orientation.
  if (args.contains("frame") && !args["frame"].is_null()) {
    std::string node_error;
    std::string node_suggestion;
    MapNode* node = mcp::ResolveNode(plan->scene, args["frame"], &node_error,
                                     &node_suggestion);
    if (!node) {
      *error = node_error;
      if (suggestion) *suggestion = node_suggestion;
      if (!node_suggestion.empty()) {
        *error += " (did you mean \"" + node_suggestion + "\"?)";
      }
      return false;
    }
    EditorScene::NodeWorldBounds bounds;
    if (!plan->scene.GetNodeWorldBounds(*node, bounds)) {
      *error = "cannot frame node without bounds: " + FullNodePath(*node);
      return false;
    }
    const glm::vec3 center = (bounds.world_min + bounds.world_max) * 0.5f;
    const float radius = std::max(
        glm::length(bounds.world_max - bounds.world_min) * 0.5f, 0.05f);
    if (desired.view_2d) {
      desired.target = glm::vec3(center.x, center.y, 0.0f);
    } else {
      desired.target = center;
      desired.distance =
          radius / std::sin(glm::radians(kFovDegrees) / 2.0f) * 1.3f;
    }
  }

  int width = 1280;
  int height = 720;
  if (!ParseDimension(args, "width", 1280, &width, error) ||
      !ParseDimension(args, "height", 720, &height, error)) {
    return false;
  }
  desired.viewport_size = glm::vec2(static_cast<float>(std::min(width, 1920)),
                                    static_cast<float>(std::min(height, 1080)));

  plan->colliders = args.contains("colliders") && args["colliders"].is_boolean()
                        ? args["colliders"].get<bool>()
                        : false;
  if (args.contains("highlight") && args["highlight"].is_array()) {
    for (const json& ref : args["highlight"]) {
      std::string node_error;
      std::string node_suggestion;
      MapNode* node =
          mcp::ResolveNode(plan->scene, ref, &node_error, &node_suggestion);
      if (!node) {
        *error = node_error;
        if (suggestion) *suggestion = node_suggestion;
        if (!node_suggestion.empty()) {
          *error += " (did you mean \"" + node_suggestion + "\"?)";
        }
        return false;
      }
      plan->highlights.push_back(plan->scene.GetEntityForNode(node->uid));
    }
  }
  plan->desired = desired;
  return true;
}

void CaptureViewport(EditorScene& scene, const json& args, ToolDone done) {
  auto plan = std::make_shared<CapturePlan>(scene);
  plan->lifetime = scene.LifetimeToken();
  plan->saved = scene.GetViewState();
  std::string error;
  std::string suggestion;
  if (!ParsePreset(args, plan.get(), &error, &suggestion)) {
    done(ToolResult{false, Error(error, suggestion)});
    return;  // no camera change: the guard has nothing to restore
  }

  RenderHost* host = scene.GetRenderHost();
  if (host) {
    // Remember the frame that was current before the camera change so the
    // real editor can wait for one rendered with the new view.
    int width = 0;
    int height = 0;
    std::vector<uint8_t> scratch;
    host->CopyScenePixels(width, height, scratch, &plan->saved_frame);
  }

  plan->camera_changed = true;
  scene.SetViewState(plan->desired);

  auto finish = [plan](ToolDone done) {
    return [plan, done = std::move(done)]() mutable {
      ToolResult result = plan->Capture();
      done(std::move(result));
      // plan's destructor runs here (or later, in the CallAfter case) and
      // restores the ViewState.
    };
  };

  if (!host || !wxTheApp) {
    // Headless tests and tools called without a wx event loop: the caller has
    // already stepped the renderer to the frame it wants captured.
    finish(std::move(done))();
    return;
  }

  // In the editor, defer by one UI turn so a frame rendered with the new
  // camera is available, then copy it. The plan (and its RAII guard) lives in
  // the closure until the call completes.
  struct Pending {
    std::shared_ptr<CapturePlan> plan;
    ToolDone done;
    RenderHost* host;
    std::weak_ptr<const void> lifetime;
    uint64_t before = 0;
    int tries = 0;
  };
  auto pending = std::make_shared<Pending>(Pending{
      plan, std::move(done), host, plan->lifetime, plan->saved_frame, 0});
  auto poll = std::make_shared<std::function<void()>>();
  *poll = [pending, poll]() {
    if (pending->lifetime.expired()) {
      // The editor is shutting down: answer the call and let the plan die
      // without touching the scene (its own guard sees the same token).
      pending->done(ToolResult{false, Error("editor shutting down")});
      return;
    }
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
    std::vector<uint8_t> scratch;
    const bool copied =
        pending->host->CopyScenePixels(width, height, scratch, &frame_id);
    if ((copied && frame_id != pending->before) ||
        ++pending->tries >= kMaxCaptureFrames) {
      ToolResult result = pending->plan->Capture();
      pending->done(std::move(result));
      return;
    }
    wxTheApp->CallAfter(*poll);
  };
  wxTheApp->CallAfter(*poll);
}

}  // namespace

MERCURY_MCP_TOOL("query_scene_spatial", kRead, &QuerySceneSpatial);
MERCURY_MCP_TOOL_ASYNC("capture_viewport", kRead, &CaptureViewport);

}  // namespace Mercury
