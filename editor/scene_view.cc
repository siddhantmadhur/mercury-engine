// The editor's Scene panel: the map rendered offscreen, an editor camera
// that works in 2D (looking down -Z: pan and zoom) or 3D (look, fly, orbit),
// click-to-select, move/rotate/scale handles, and gizmos for things that draw
// nothing themselves (colliders, cameras, lights, empty nodes).

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>

#include "editor_scene.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "properties/mesh.h"

namespace Mercury {

namespace {

constexpr float kEditorFov = 60.0f;
constexpr float kPickRadiusPixels = 7.0f;
constexpr float kHandlePixels = 85.0f;  // length of move and scale handles
constexpr float kRingPixels = 70.0f;    // radius of rotate rings
constexpr float kHandleHitPixels = 7.0f;
constexpr float kPi = std::numbers::pi_v<float>;
const glm::vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// Handle ids: 0-2 are the X/Y/Z axis handles (or rings), 3 is the center.
constexpr int kCenterHandle = 3;

ImU32 Rgba(float r, float g, float b, float a) {
  return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

ImU32 AxisColor(int axis, bool highlighted, float alpha = 1.0f) {
  if (highlighted) return Rgba(1.0f, 0.85f, 0.2f, 1.0f);
  switch (axis) {
    case 0: return Rgba(0.95f, 0.3f, 0.3f, alpha);
    case 1: return Rgba(0.4f, 0.9f, 0.3f, alpha);
    default: return Rgba(0.3f, 0.55f, 1.0f, alpha);
  }
}

glm::vec2 V2(ImVec2 v) { return {v.x, v.y}; }

float DistanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
  glm::vec2 ab = b - a;
  float t = glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f);
  t = std::clamp(t, 0.0f, 1.0f);
  return glm::length(p - (a + ab * t));
}

// Distance along the ray to where it enters the box, if it hits.
std::optional<float> RayBox(glm::vec3 origin, glm::vec3 direction,
                            glm::vec3 min, glm::vec3 max) {
  float t_min = 0.0f;
  float t_max = std::numeric_limits<float>::max();
  for (int i = 0; i < 3; i++) {
    if (std::abs(direction[i]) < 1e-9f) {
      if (origin[i] < min[i] || origin[i] > max[i]) return std::nullopt;
      continue;
    }
    float t1 = (min[i] - origin[i]) / direction[i];
    float t2 = (max[i] - origin[i]) / direction[i];
    if (t1 > t2) std::swap(t1, t2);
    t_min = std::max(t_min, t1);
    t_max = std::min(t_max, t2);
    if (t_min > t_max) return std::nullopt;
  }
  return t_min;
}

// The distance along the line `origin + s * direction` that comes closest
// to the ray; how axis handles follow the mouse.
float ClosestOnLine(glm::vec3 origin, glm::vec3 direction, glm::vec3 ray_origin,
                    glm::vec3 ray_direction) {
  const glm::vec3 w = origin - ray_origin;
  const float a = glm::dot(direction, direction);
  const float b = glm::dot(direction, ray_direction);
  const float c = glm::dot(ray_direction, ray_direction);
  const float d = glm::dot(direction, w);
  const float e = glm::dot(ray_direction, w);
  const float denominator = a * c - b * b;
  if (std::abs(denominator) < 1e-9f) return 0.0f;  // looking along the axis
  return (b * e - c * d) / denominator;
}

std::optional<glm::vec3> RayPlane(glm::vec3 ray_origin, glm::vec3 ray_direction,
                                  glm::vec3 point, glm::vec3 normal) {
  const float denominator = glm::dot(normal, ray_direction);
  if (std::abs(denominator) < 1e-6f) return std::nullopt;
  const float t = glm::dot(point - ray_origin, normal) / denominator;
  if (t < 0.0f) return std::nullopt;
  return ray_origin + ray_direction * t;
}

// Two unit vectors perpendicular to `axis` and to each other.
void Basis(glm::vec3 axis, glm::vec3& u, glm::vec3& v) {
  axis = glm::normalize(axis);
  const glm::vec3 helper =
      std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  u = glm::normalize(glm::cross(helper, axis));
  v = glm::cross(axis, u);
}

float WrapAngle(float radians) {
  while (radians > kPi) radians -= 2.0f * kPi;
  while (radians < -kPi) radians += 2.0f * kPi;
  return radians;
}

// Rounds to a multiple of `step`. Fractional steps divide by a whole number
// so 3.702 comes out as the float nearest 3.702, not 3702 * 0.001f.
glm::vec3 Round(glm::vec3 value, float step) {
  if (step < 1.0f) {
    const float per_unit = std::round(1.0f / step);
    return glm::round(value * per_unit) / per_unit;
  }
  return glm::round(value / step) * step;
}

bool GetVec3(const MapNode& node, const char* key, glm::vec3& out) {
  auto transform = node.FindComponent("Transform");
  if (!transform) return false;
  auto value = transform->properties.Get(key);
  if (!value) return false;
  auto v = value->Get<glm::vec3>();
  if (!v) return false;
  out = *v;
  return true;
}

}  // namespace

// ----------------------------------------------------------------- Camera ---

glm::vec3 EditorScene::CameraForward() const {
  if (view_2d_) return glm::vec3(0.0f, 0.0f, -1.0f);
  return Transform::EulerToQuat(glm::vec3(camera_pitch_, camera_yaw_, 0.0f)) *
         glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 EditorScene::CameraPosition() const {
  return camera_target_ - CameraForward() * camera_distance_;
}

void EditorScene::UpdateEditorCamera() {
  const kEntityId entity = GetDefaultCamera();
  auto transform = Objects().GetProperty<Transform>(entity);
  auto camera = Objects().GetProperty<Camera>(entity);
  if (!transform || !camera) return;
  camera->SetUseObjectRotation(true);
  camera->SetFOV(kEditorFov);
  camera->SetClipPlanes(std::clamp(camera_distance_ * 0.002f, 0.01f, 1.0f),
                        std::max(2000.0f, camera_distance_ * 20.0f));
  transform->SetPosition(CameraPosition());
  transform->SetRotate(view_2d_ ? glm::vec3(0.0f)
                                : glm::vec3(camera_pitch_, camera_yaw_, 0.0f));
  transform->SnapInterpolation();
  transform->ClearTransformations();
}

glm::mat4 EditorScene::GetViewProjection() const {
  auto* self = const_cast<EditorScene*>(this);
  auto transform = self->Objects().GetProperty<Transform>(GetDefaultCamera());
  auto camera = self->Objects().GetProperty<Camera>(GetDefaultCamera());
  if (!transform || !camera) return glm::mat4(1.0f);
  return camera->GetProjection(std::max(scene_size_.x, 1.0f),
                               std::max(scene_size_.y, 1.0f)) *
         camera->GetView(*transform);
}

EditorScene::Ray EditorScene::ScreenRay(ImVec2 pixel) const {
  const glm::mat4 inverse = glm::inverse(GetViewProjection());
  const float x = (pixel.x - scene_origin_.x) / scene_size_.x * 2.0f - 1.0f;
  const float y = 1.0f - (pixel.y - scene_origin_.y) / scene_size_.y * 2.0f;
  glm::vec4 near_point = inverse * glm::vec4(x, y, 0.0f, 1.0f);
  glm::vec4 far_point = inverse * glm::vec4(x, y, 1.0f, 1.0f);
  near_point /= near_point.w;
  far_point /= far_point.w;
  return {glm::vec3(near_point),
          glm::normalize(glm::vec3(far_point) - glm::vec3(near_point))};
}

bool EditorScene::WorldToScreen(glm::vec3 world, ImVec2& pixel) const {
  const glm::vec4 clip = GetViewProjection() * glm::vec4(world, 1.0f);
  if (clip.w <= 1e-6f) return false;
  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  pixel = ImVec2(scene_origin_.x + (ndc.x + 1.0f) * 0.5f * scene_size_.x,
                 scene_origin_.y + (1.0f - ndc.y) * 0.5f * scene_size_.y);
  return true;
}

float EditorScene::WorldPerPixel(glm::vec3 point) const {
  const float depth =
      std::max(glm::dot(point - CameraPosition(), CameraForward()), 1e-3f);
  return 2.0f * depth * std::tan(glm::radians(kEditorFov) / 2.0f) /
         std::max(scene_size_.y, 1.0f);
}

// ---------------------------------------------------------------- Drawing ---

void EditorScene::DrawLine3D(ImDrawList* draw, glm::vec3 a, glm::vec3 b,
                             ImU32 color, float thickness) const {
  const glm::mat4 view_projection = GetViewProjection();
  glm::vec4 ca = view_projection * glm::vec4(a, 1.0f);
  glm::vec4 cb = view_projection * glm::vec4(b, 1.0f);
  // Clip against the near plane and the sides of the view, so lines that
  // pass behind the camera or far off screen still draw correctly.
  auto distance = [](const glm::vec4& c, int plane) {
    switch (plane) {
      case 0: return c.z;  // near; the depth range is 0 to 1
      case 1: return c.w + c.x;
      case 2: return c.w - c.x;
      case 3: return c.w + c.y;
      default: return c.w - c.y;
    }
  };
  for (int plane = 0; plane < 5; plane++) {
    const float da = distance(ca, plane);
    const float db = distance(cb, plane);
    if (da < 0.0f && db < 0.0f) return;
    if (da < 0.0f) {
      ca = glm::mix(ca, cb, da / (da - db));
    } else if (db < 0.0f) {
      cb = glm::mix(cb, ca, db / (db - da));
    }
  }
  if (ca.w <= 1e-6f || cb.w <= 1e-6f) return;
  auto to_pixel = [&](const glm::vec4& c) {
    return ImVec2(scene_origin_.x + (c.x / c.w + 1.0f) * 0.5f * scene_size_.x,
                  scene_origin_.y + (1.0f - c.y / c.w) * 0.5f * scene_size_.y);
  };
  draw->AddLine(to_pixel(ca), to_pixel(cb), color, thickness);
}

void EditorScene::DrawCircle3D(ImDrawList* draw, glm::vec3 center,
                               glm::vec3 axis, float radius, ImU32 color,
                               float thickness) const {
  glm::vec3 u, v;
  Basis(axis, u, v);
  constexpr int kSegments = 48;
  glm::vec3 previous = center + u * radius;
  for (int i = 1; i <= kSegments; i++) {
    const float angle = static_cast<float>(i) / kSegments * 2.0f * kPi;
    const glm::vec3 point =
        center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
    DrawLine3D(draw, previous, point, color, thickness);
    previous = point;
  }
}

void EditorScene::DrawBox3D(ImDrawList* draw, const glm::mat4& model,
                            glm::vec3 min, glm::vec3 max, ImU32 color,
                            float thickness) const {
  glm::vec3 corners[8];
  for (int i = 0; i < 8; i++) {
    const glm::vec3 local(i & 1 ? max.x : min.x, i & 2 ? max.y : min.y,
                          i & 4 ? max.z : min.z);
    corners[i] = glm::vec3(model * glm::vec4(local, 1.0f));
  }
  static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7},
                                        {0, 2}, {1, 3}, {4, 6}, {5, 7},
                                        {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const auto& edge : kEdges) {
    DrawLine3D(draw, corners[edge[0]], corners[edge[1]], color, thickness);
  }
}

void EditorScene::DrawGrid(ImDrawList* draw) {
  if (view_2d_) {
    // The z = 0 plane, with lines every power of ten that keeps them at
    // least ~40px apart. X axis red, Y axis green.
    const glm::vec3 center(camera_target_.x, camera_target_.y, 0.0f);
    const float world_per_pixel = WorldPerPixel(center);
    const float spacing =
        std::pow(10.0f, std::ceil(std::log10(40.0f * world_per_pixel)));
    const glm::vec2 half =
        glm::vec2(scene_size_.x, scene_size_.y) * 0.5f * world_per_pixel;
    const glm::vec2 min = glm::vec2(center) - half;
    const glm::vec2 max = glm::vec2(center) + half;
    const ImU32 minor = Rgba(1, 1, 1, 0.05f);
    for (float x = std::floor(min.x / spacing) * spacing; x <= max.x; x += spacing) {
      DrawLine3D(draw, {x, min.y, 0}, {x, max.y, 0}, minor);
    }
    for (float y = std::floor(min.y / spacing) * spacing; y <= max.y; y += spacing) {
      DrawLine3D(draw, {min.x, y, 0}, {max.x, y, 0}, minor);
    }
    DrawLine3D(draw, {min.x, 0, 0}, {max.x, 0, 0}, AxisColor(0, false, 0.35f));
    DrawLine3D(draw, {0, min.y, 0}, {0, max.y, 0}, AxisColor(1, false, 0.35f));
    return;
  }

  // The y = 0 plane around where the camera looks; every tenth line is
  // brighter. X axis red, Z axis blue.
  const glm::vec3 eye = CameraPosition();
  const float height = std::max(std::abs(eye.y), camera_distance_ * 0.25f);
  const float spacing =
      std::pow(10.0f, std::floor(std::log10(std::max(height, 1e-3f) / 2.0f)));
  constexpr int kLines = 50;
  const float extent = spacing * kLines;
  const float major_step = spacing * 10.0f;
  const glm::vec2 center(std::round(camera_target_.x / major_step) * major_step,
                         std::round(camera_target_.z / major_step) * major_step);
  const ImU32 minor = Rgba(1, 1, 1, 0.06f);
  const ImU32 major = Rgba(1, 1, 1, 0.14f);
  for (int i = -kLines; i <= kLines; i++) {
    const float x = center.x + i * spacing;
    const float z = center.y + i * spacing;
    const int index_x = static_cast<int>(std::lround(x / spacing));
    const int index_z = static_cast<int>(std::lround(z / spacing));
    if (index_x != 0) {
      DrawLine3D(draw, {x, 0, center.y - extent}, {x, 0, center.y + extent},
                 index_x % 10 == 0 ? major : minor);
    }
    if (index_z != 0) {
      DrawLine3D(draw, {center.x - extent, 0, z}, {center.x + extent, 0, z},
                 index_z % 10 == 0 ? major : minor);
    }
  }
  DrawLine3D(draw, {center.x - extent, 0, 0}, {center.x + extent, 0, 0},
             AxisColor(0, false, 0.55f));
  DrawLine3D(draw, {0, 0, center.y - extent}, {0, 0, center.y + extent},
             AxisColor(2, false, 0.55f));
}

// ------------------------------------------------------------ Node bounds ---

glm::mat4 EditorScene::GetNodeMatrix(kEntityId entity, bool with_scale) {
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return glm::mat4(1.0f);
  glm::mat4 model = glm::translate(glm::mat4(1.0f), transform->GetPosition()) *
                    glm::mat4_cast(transform->GetRotationQuat());
  if (with_scale) {
    // Keep the matrix invertible for flat objects.
    glm::vec3 scale = transform->GetScale();
    for (int i = 0; i < 3; i++) {
      if (std::abs(scale[i]) < 1e-4f) scale[i] = scale[i] < 0 ? -1e-4f : 1e-4f;
    }
    model = glm::scale(model, scale);
  }
  return model;
}

bool EditorScene::GetNodeBounds(kEntityId entity, glm::mat4& model,
                                glm::vec3& min, glm::vec3& max) {
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return false;
  if (auto mesh = Objects().GetProperty<Mesh>(entity)) {
    model = GetNodeMatrix(entity, true);
    const mAABB aabb = mesh->GetAABB();
    min = aabb.min;
    max = aabb.max;
    return true;
  }
  // Collider sizes are in world units once scaled.
  const glm::vec3 scale = glm::abs(transform->GetScale());
  glm::vec3 half(0.0f);
  if (auto box = Objects().GetProperty<BoxCollider>(entity)) {
    half = glm::abs(box->size * scale) * 0.5f;
  } else if (auto sphere = Objects().GetProperty<SphereCollider>(entity)) {
    half = glm::vec3(sphere->radius * std::max({scale.x, scale.y, scale.z}));
  } else if (auto capsule = Objects().GetProperty<CapsuleCollider>(entity)) {
    const float radius = capsule->radius * std::max(scale.x, scale.z);
    half = glm::vec3(radius, std::max(capsule->height * scale.y * 0.5f, radius),
                     radius);
  } else if (auto cylinder = Objects().GetProperty<CylinderCollider>(entity)) {
    const float radius = cylinder->radius * std::max(scale.x, scale.z);
    half = glm::vec3(radius, cylinder->height * scale.y * 0.5f, radius);
  } else {
    return false;
  }
  model = GetNodeMatrix(entity, false);
  min = -half;
  max = half;
  return true;
}

// ---------------------------------------------------------------- Picking ---

MapNode* EditorScene::PickNode(ImVec2 mouse) {
  const Ray ray = ScreenRay(mouse);
  const glm::vec2 m = V2(mouse);

  // Lower tier wins: things with bounds (nearest first, then smallest), then
  // empty-node markers, then wall planes, then 2D camera frames.
  enum Tier { kBounds, kMarker, kPlane, kCameraFrame };
  MapNode* best = nullptr;
  int best_tier = std::numeric_limits<int>::max();
  float best_distance = 0.0f;
  float best_size = 0.0f;
  auto consider = [&](MapNode& node, Tier tier, float distance, float size) {
    bool better = tier < best_tier;
    if (tier == best_tier) {
      const float tolerance = 1e-4f * std::max(1.0f, std::abs(best_distance));
      if (distance < best_distance - tolerance) {
        better = true;
      } else if (std::abs(distance - best_distance) <= tolerance) {
        // Same depth (sprites on one plane): the smaller one, so the ball
        // can be picked in front of a wall.
        better = size < best_size;
      }
    }
    if (better) {
      best = &node;
      best_tier = tier;
      best_distance = distance;
      best_size = size;
    }
  };

  map_.Visit([&](MapNode& node) {
    if (node.IsRoot()) return;
    kEntityId entity = GetEntityForNode(node.uid);
    auto transform = Objects().GetProperty<Transform>(entity);
    if (!transform) return;
    const glm::vec3 position = transform->GetPosition();

    glm::mat4 model;
    glm::vec3 min, max;
    if (GetNodeBounds(entity, model, min, max)) {
      // Give flat shapes a little thickness so they can be hit edge-on.
      for (int i = 0; i < 3; i++) {
        if (max[i] - min[i] < 1e-4f) {
          min[i] -= 1e-3f;
          max[i] += 1e-3f;
        }
      }
      // The ray in the shape's own space; t means the same distance there.
      const glm::mat4 inverse = glm::inverse(model);
      const glm::vec3 local_origin(inverse * glm::vec4(ray.origin, 1.0f));
      const glm::vec3 local_direction(inverse * glm::vec4(ray.direction, 0.0f));
      if (auto t = RayBox(local_origin, local_direction, min, max)) {
        const glm::vec3 extent =
            glm::abs(glm::vec3(model * glm::vec4(max - min, 0.0f)));
        const float size = std::max(extent.x, 1e-3f) *
                           std::max(extent.y, 1e-3f) * std::max(extent.z, 1e-3f);
        consider(node, kBounds, *t, size);
      }
      return;
    }

    if (auto plane = Objects().GetProperty<PlaneCollider>(entity)) {
      const glm::vec3 normal = plane->normal;
      if (view_2d_ && glm::length(glm::vec2(normal)) > 1e-4f &&
          std::abs(normal.z) < 1e-4f) {
        // Seen edge-on in 2D: a line through the node.
        const glm::vec2 n = glm::normalize(glm::vec2(normal));
        const glm::vec3 along(-n.y, n.x, 0.0f);
        const float reach = 1e4f * WorldPerPixel(position);
        ImVec2 a, b;
        if (WorldToScreen(position - along * reach, a) &&
            WorldToScreen(position + along * reach, b)) {
          const float distance = DistanceToSegment(m, V2(a), V2(b));
          if (distance < kPickRadiusPixels) consider(node, kPlane, distance, 0);
        }
        return;
      }
    }
    if (auto camera = Objects().GetProperty<CameraComponent>(entity);
        camera && camera->fit_height > 0.0f) {
      const float half_h = camera->fit_height * 0.5f;
      const float half_w = half_h * game_aspect_;
      const glm::vec3 c(position.x, position.y, 0.0f);
      const glm::vec3 corners[4] = {c + glm::vec3(-half_w, -half_h, 0),
                                    c + glm::vec3(half_w, -half_h, 0),
                                    c + glm::vec3(half_w, half_h, 0),
                                    c + glm::vec3(-half_w, half_h, 0)};
      ImVec2 projected[4];
      bool visible = true;
      for (int i = 0; i < 4; i++) visible &= WorldToScreen(corners[i], projected[i]);
      if (visible) {
        float distance = std::numeric_limits<float>::max();
        for (int i = 0; i < 4; i++) {
          distance = std::min(distance, DistanceToSegment(m, V2(projected[i]),
                                                          V2(projected[(i + 1) % 4])));
        }
        if (distance < kPickRadiusPixels) {
          consider(node, kCameraFrame, distance, 0);
          return;
        }
      }
    }
    ImVec2 screen;
    if (WorldToScreen(position, screen)) {
      const float distance = glm::length(m - V2(screen));
      if (distance < kPickRadiusPixels * 1.8f) consider(node, kMarker, distance, 0);
    }
  });
  return best;
}

// ---------------------------------------------------------------- Handles ---

bool EditorScene::BeginHandleDrag(ImVec2 mouse) {
  MapNode* node = GetSelectedNode();
  if (!node || hovered_handle_ < 0) return false;
  kEntityId entity = GetEntityForNode(node->uid);
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return false;

  drag_origin_ = transform->GetPosition();
  drag_start_mouse_ = mouse;
  drag_start_local_ = glm::vec3(0.0f);
  drag_start_rotation_ = glm::vec3(0.0f);
  drag_start_scale_ = glm::vec3(1.0f);
  GetVec3(*node, "position", drag_start_local_);
  GetVec3(*node, "rotation", drag_start_rotation_);
  GetVec3(*node, "scale", drag_start_scale_);

  const Ray ray = ScreenRay(mouse);
  drag_axis_ = hovered_handle_;
  switch (tool_) {
    case Tool::kMove:
      if (hovered_handle_ == kCenterHandle) {
        drag_ = Drag::kPlane;
        drag_plane_normal_ = view_2d_ ? glm::vec3(0, 0, 1) : -CameraForward();
        auto hit = RayPlane(ray.origin, ray.direction, drag_origin_,
                            drag_plane_normal_);
        drag_start_world_ = hit ? *hit : drag_origin_;
      } else {
        drag_ = Drag::kAxis;
        drag_axis_dir_ = kAxes[hovered_handle_];
        drag_start_param_ = ClosestOnLine(drag_origin_, drag_axis_dir_,
                                          ray.origin, ray.direction);
      }
      break;
    case Tool::kRotate: {
      drag_ = Drag::kRotate;
      drag_axis_dir_ = kAxes[hovered_handle_];
      ImVec2 center;
      WorldToScreen(drag_origin_, center);
      drag_start_param_ = std::atan2(mouse.y - center.y, mouse.x - center.x);
      drag_accum_ = 0.0f;
      break;
    }
    case Tool::kScale:
      if (hovered_handle_ == kCenterHandle) {
        drag_ = Drag::kUniformScale;
      } else {
        drag_ = Drag::kScale;
        drag_axis_dir_ = transform->GetRotationQuat() * kAxes[hovered_handle_];
        drag_length_ =
            std::max(kHandlePixels * WorldPerPixel(drag_origin_), 1e-6f);
        drag_start_param_ = ClosestOnLine(drag_origin_, drag_axis_dir_,
                                          ray.origin, ray.direction);
      }
      break;
  }
  return true;
}

void EditorScene::SetSelectedTransform(const char* key, glm::vec3 value) {
  MapNode* node = GetSelectedNode();
  if (!node) return;
  // Keep saved maps readable: no long fractions from mouse movement.
  value = Round(value, 1e-3f) + glm::vec3(0.0f);  // + 0 turns -0 into 0
  ComponentData* transform = node->FindComponent("Transform");
  if (!transform) {
    node->components.insert(node->components.begin(), {"Transform", {}});
    transform = &node->components.front();
  }
  const mtext::Variant* current = transform->properties.Get(key);
  if (!current || !(*current == mtext::Variant(value))) {
    transform->properties.Set(key, value);
    MarkChanged();
  }
}

void EditorScene::UpdateDrag(ImVec2 mouse) {
  const Ray ray = ScreenRay(mouse);
  const bool snap = ImGui::GetIO().KeyCtrl;
  const float move_snap = view_2d_ ? 1.0f : 0.5f;

  switch (drag_) {
    case Drag::kBody:
    case Drag::kPlane: {
      auto hit = RayPlane(ray.origin, ray.direction, drag_origin_,
                          drag_plane_normal_);
      if (!hit) return;
      glm::vec3 position = drag_start_local_ + (*hit - drag_start_world_);
      if (snap) position = Round(position, move_snap);
      if (view_2d_) position.z = drag_start_local_.z;
      SetSelectedTransform("position", position);
      break;
    }
    case Drag::kAxis: {
      const float delta = ClosestOnLine(drag_origin_, drag_axis_dir_, ray.origin,
                                        ray.direction) -
                          drag_start_param_;
      glm::vec3 position = drag_start_local_ + drag_axis_dir_ * delta;
      if (snap) {
        position[drag_axis_] = Round(position, move_snap)[drag_axis_];
      }
      SetSelectedTransform("position", position);
      break;
    }
    case Drag::kRotate: {
      ImVec2 center;
      if (!WorldToScreen(drag_origin_, center)) return;
      const float angle = std::atan2(mouse.y - center.y, mouse.x - center.x);
      drag_accum_ += WrapAngle(angle - drag_start_param_);
      drag_start_param_ = angle;
      // Screen y points down, so turning counter-clockwise on screen lowers
      // the angle. Seen from the tip of an axis, positive turns are
      // counter-clockwise.
      const bool tip_faces_camera =
          glm::dot(drag_axis_dir_, CameraForward()) < 0.0f;
      float degrees = glm::degrees(-drag_accum_) * (tip_faces_camera ? 1.0f : -1.0f);
      if (snap) degrees = std::round(degrees / 15.0f) * 15.0f;
      glm::vec3 euler;
      if (drag_axis_ == 2 && drag_start_rotation_.x == 0.0f &&
          drag_start_rotation_.y == 0.0f) {
        // Turning a flat object around Z (always so in 2D): keep a plain Z
        // angle rather than an equivalent mix of all three.
        euler = glm::vec3(0.0f, 0.0f, drag_start_rotation_.z + degrees);
      } else {
        const glm::quat rotation =
            glm::angleAxis(glm::radians(degrees), drag_axis_dir_) *
            Transform::EulerToQuat(drag_start_rotation_);
        euler = Transform::QuatToEuler(rotation);
      }
      SetSelectedTransform("rotation", Round(euler, 0.01f));
      break;
    }
    case Drag::kScale: {
      const float delta = ClosestOnLine(drag_origin_, drag_axis_dir_, ray.origin,
                                        ray.direction) -
                          drag_start_param_;
      glm::vec3 scale = drag_start_scale_;
      scale[drag_axis_] *= std::max(1.0f + delta / drag_length_, 0.01f);
      if (snap) scale[drag_axis_] = Round(scale, 0.1f)[drag_axis_];
      SetSelectedTransform("scale", scale);
      break;
    }
    case Drag::kUniformScale: {
      const float factor =
          std::max(1.0f + (mouse.x - drag_start_mouse_.x) / 100.0f, 0.01f);
      glm::vec3 scale = drag_start_scale_ * factor;
      if (snap) scale = Round(scale, 0.1f);
      SetSelectedTransform("scale", scale);
      break;
    }
    case Drag::kNone:
    case Drag::kOrbit:
      break;
  }
}

void EditorScene::DrawToolHandles(ImDrawList* draw) {
  const bool dragging_handle = drag_ != Drag::kNone && drag_ != Drag::kBody &&
                               drag_ != Drag::kOrbit;
  if (!dragging_handle) hovered_handle_ = -1;
  MapNode* node = GetSelectedNode();
  if (!node) return;
  kEntityId entity = GetEntityForNode(node->uid);
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return;

  const glm::vec3 origin = transform->GetPosition();
  ImVec2 center;
  if (!WorldToScreen(origin, center)) return;
  const glm::vec2 mouse = V2(ImGui::GetIO().MousePos);
  const float world_per_pixel = WorldPerPixel(origin);
  const bool can_hover = !dragging_handle && drag_ != Drag::kBody &&
                         // The viewport goes active on the frame it is clicked.
                         ImGui::IsWindowHovered(
                             ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         !ImGui::GetIO().KeyAlt &&
                         !ImGui::IsMouseDown(ImGuiMouseButton_Right);
  auto active = [&](int handle) {
    return dragging_handle ? drag_axis_ == handle : hovered_handle_ == handle;
  };

  if (tool_ == Tool::kRotate) {
    const float radius = kRingPixels * world_per_pixel;
    // Find the ring under the mouse first so it can be highlighted.
    if (can_hover) {
      float best = kHandleHitPixels;
      for (int axis = 0; axis < 3; axis++) {
        if (view_2d_ && axis != 2) continue;
        glm::vec3 u, v;
        Basis(kAxes[axis], u, v);
        ImVec2 previous;
        bool have_previous = false;
        for (int i = 0; i <= 48; i++) {
          const float angle = static_cast<float>(i) / 48 * 2.0f * kPi;
          ImVec2 point;
          const bool ok = WorldToScreen(
              origin + (u * std::cos(angle) + v * std::sin(angle)) * radius, point);
          if (ok && have_previous) {
            const float distance = DistanceToSegment(mouse, V2(previous), V2(point));
            if (distance < best) {
              best = distance;
              hovered_handle_ = axis;
            }
          }
          previous = point;
          have_previous = ok;
        }
      }
    }
    for (int axis = 0; axis < 3; axis++) {
      if (view_2d_ && axis != 2) continue;
      DrawCircle3D(draw, origin, kAxes[axis], radius,
                   AxisColor(axis, active(axis)), active(axis) ? 3.0f : 2.0f);
    }
    draw->AddCircle(center, 3.0f, Rgba(1, 1, 1, 0.8f));
    return;
  }

  // Move handles follow the world axes; scale handles the node's own axes.
  const float length = kHandlePixels * world_per_pixel;
  const glm::quat rotation = transform->GetRotationQuat();
  glm::vec3 directions[3];
  for (int axis = 0; axis < 3; axis++) {
    directions[axis] = tool_ == Tool::kScale ? rotation * kAxes[axis] : kAxes[axis];
  }

  // Hover: the center square first, then the closest axis.
  if (can_hover) {
    if (std::abs(mouse.x - center.x) < 8.0f && std::abs(mouse.y - center.y) < 8.0f) {
      hovered_handle_ = kCenterHandle;
    } else {
      float best = kHandleHitPixels;
      for (int axis = 0; axis < 3; axis++) {
        if (view_2d_ && axis == 2) continue;
        ImVec2 tip;
        if (!WorldToScreen(origin + directions[axis] * length, tip)) continue;
        const float distance = DistanceToSegment(mouse, V2(center), V2(tip));
        if (distance < best) {
          best = distance;
          hovered_handle_ = axis;
        }
      }
    }
  }

  for (int axis = 0; axis < 3; axis++) {
    if (view_2d_ && axis == 2) continue;
    ImVec2 tip;
    if (!WorldToScreen(origin + directions[axis] * length, tip)) continue;
    const ImU32 color = AxisColor(axis, active(axis));
    draw->AddLine(center, tip, color, active(axis) ? 3.5f : 2.5f);
    if (tool_ == Tool::kScale) {
      draw->AddRectFilled(ImVec2(tip.x - 5, tip.y - 5), ImVec2(tip.x + 5, tip.y + 5),
                          color);
      continue;
    }
    // An arrow head along the axis as it appears on screen.
    glm::vec2 direction = V2(tip) - V2(center);
    const float pixels = glm::length(direction);
    if (pixels < 1.0f) continue;
    direction /= pixels;
    const glm::vec2 side(-direction.y, direction.x);
    const glm::vec2 point = V2(tip) + direction * 11.0f;
    const glm::vec2 left = V2(tip) + side * 5.0f;
    const glm::vec2 right = V2(tip) - side * 5.0f;
    draw->AddTriangleFilled(ImVec2(point.x, point.y), ImVec2(left.x, left.y),
                            ImVec2(right.x, right.y), color);
  }
  draw->AddRect(ImVec2(center.x - 6, center.y - 6), ImVec2(center.x + 6, center.y + 6),
                active(kCenterHandle) ? AxisColor(0, true) : Rgba(1, 1, 1, 0.75f),
                0.0f, 0, 2.0f);
}

// ----------------------------------------------------------------- Gizmos ---

void EditorScene::DrawNodeGizmos(ImDrawList* draw, const MapNode& node) {
  kEntityId entity = GetEntityForNode(node.uid);
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return;
  const glm::vec3 position = transform->GetPosition();
  const glm::vec3 scale = glm::abs(transform->GetScale());
  const glm::quat rotation = transform->GetRotationQuat();
  const glm::mat4 placed = GetNodeMatrix(entity, false);
  const bool selected = node.uid == selected_uid_;
  const float world_per_pixel = WorldPerPixel(position);
  const bool has_mesh = Objects().GetProperty<Mesh>(entity) != nullptr;
  bool drew_something = has_mesh;
  // Colliders of visible things only clutter the view, so like Unity they
  // are outlined only when selected.
  const bool show_colliders = selected || !has_mesh;

  const ImU32 collider_color = Rgba(0.35f, 0.95f, 0.45f, 0.65f);
  const ImU32 camera_color = Rgba(0.5f, 0.8f, 1.0f, 0.8f);
  const ImU32 light_color = Rgba(1.0f, 0.85f, 0.35f, 0.9f);
  const ImU32 selection_color = Rgba(1.0f, 0.6f, 0.1f, 1.0f);
  auto local = [&](glm::vec3 offset) { return position + rotation * offset; };

  if (auto plane = Objects().GetProperty<PlaneCollider>(entity)) {
    glm::vec3 normal = plane->normal;
    if (glm::length(normal) > 1e-4f) {
      normal = glm::normalize(normal);
      const ImU32 color = selected ? selection_color : collider_color;
      const float thickness = selected ? 2.5f : 1.5f;
      if (view_2d_ && std::abs(normal.z) < 1e-4f) {
        // Edge-on: an endless line.
        const glm::vec3 along(-normal.y, normal.x, 0.0f);
        const float reach = 1e4f * world_per_pixel;
        DrawLine3D(draw, position - along * reach, position + along * reach, color,
                   thickness);
      } else {
        // A patch of the endless plane.
        glm::vec3 u, v;
        Basis(normal, u, v);
        const float half = 60.0f * world_per_pixel;
        const glm::vec3 corners[4] = {
            position + (u + v) * half, position + (u - v) * half,
            position - (u + v) * half, position - (u - v) * half};
        for (int i = 0; i < 4; i++) {
          DrawLine3D(draw, corners[i], corners[(i + 1) % 4], color, thickness);
        }
      }
      DrawLine3D(draw, position, position + normal * 24.0f * world_per_pixel,
                 collider_color, 1.5f);
      drew_something = true;
    }
  }
  if (auto box = show_colliders ? Objects().GetProperty<BoxCollider>(entity) : nullptr) {
    const glm::vec3 half = glm::abs(box->size * scale) * 0.5f;
    DrawBox3D(draw, placed, -half, half, collider_color);
    drew_something = true;
  }
  if (auto sphere =
          show_colliders ? Objects().GetProperty<SphereCollider>(entity) : nullptr) {
    const float radius = sphere->radius * std::max({scale.x, scale.y, scale.z});
    for (const auto& axis : kAxes) {
      DrawCircle3D(draw, position, rotation * axis, radius, collider_color);
    }
    drew_something = true;
  }
  // Capsules and cylinders: rings at both ends joined by four lines, plus
  // round caps for capsules.
  auto draw_upright = [&](float radius, float half_height, bool round_caps) {
    const glm::vec3 up = rotation * glm::vec3(0, 1, 0);
    DrawCircle3D(draw, local({0, half_height, 0}), up, radius, collider_color);
    DrawCircle3D(draw, local({0, -half_height, 0}), up, radius, collider_color);
    const glm::vec3 sides[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& side : sides) {
      DrawLine3D(draw, local(side * radius + glm::vec3(0, half_height, 0)),
                 local(side * radius - glm::vec3(0, half_height, 0)), collider_color);
    }
    if (!round_caps) return;
    for (float end : {1.0f, -1.0f}) {
      for (const glm::vec3& side : {glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)}) {
        glm::vec3 previous = local(side * radius + glm::vec3(0, end * half_height, 0));
        for (int i = 1; i <= 16; i++) {
          const float angle = static_cast<float>(i) / 16 * kPi;
          const glm::vec3 point =
              local(side * radius * std::cos(angle) +
                    glm::vec3(0, end * (half_height + radius * std::sin(angle)), 0));
          DrawLine3D(draw, previous, point, collider_color);
          previous = point;
        }
      }
    }
  };
  if (auto capsule =
          show_colliders ? Objects().GetProperty<CapsuleCollider>(entity) : nullptr) {
    const float radius = capsule->radius * std::max(scale.x, scale.z);
    draw_upright(radius, std::max(capsule->height * scale.y * 0.5f - radius, 0.0f),
                 true);
    drew_something = true;
  }
  if (auto cylinder =
          show_colliders ? Objects().GetProperty<CylinderCollider>(entity) : nullptr) {
    draw_upright(cylinder->radius * std::max(scale.x, scale.z),
                 cylinder->height * scale.y * 0.5f, false);
    drew_something = true;
  }

  if (auto camera = Objects().GetProperty<CameraComponent>(entity)) {
    const ImU32 color = selected ? selection_color : camera_color;
    const float thickness = selected ? 2.5f : 1.5f;
    if (camera->fit_height > 0.0f) {
      // A 2D camera: the frame it shows on the z = 0 plane.
      const float half_h = camera->fit_height * 0.5f;
      const float half_w = half_h * game_aspect_;
      const glm::vec3 c(position.x, position.y, 0.0f);
      const glm::vec3 corners[4] = {
          c + glm::vec3(-half_w, half_h, 0), c + glm::vec3(half_w, half_h, 0),
          c + glm::vec3(half_w, -half_h, 0), c + glm::vec3(-half_w, -half_h, 0)};
      for (int i = 0; i < 4; i++) {
        DrawLine3D(draw, corners[i], corners[(i + 1) % 4], color, thickness);
      }
      ImVec2 label;
      if (WorldToScreen(corners[0], label)) {
        draw->AddText(ImVec2(label.x + 4, label.y + 2), camera_color,
                      node.name.c_str());
      }
    } else {
      // A 3D camera: a small view frustum with a notch on top.
      const float depth = 50.0f * world_per_pixel;
      const float half_h = depth * std::tan(glm::radians(camera->fov) / 2.0f);
      const float half_w = half_h * game_aspect_;
      const glm::vec3 corners[4] = {
          local({-half_w, half_h, -depth}), local({half_w, half_h, -depth}),
          local({half_w, -half_h, -depth}), local({-half_w, -half_h, -depth})};
      for (int i = 0; i < 4; i++) {
        DrawLine3D(draw, position, corners[i], color, thickness);
        DrawLine3D(draw, corners[i], corners[(i + 1) % 4], color, thickness);
      }
      DrawLine3D(draw, local({-half_w * 0.4f, half_h * 1.1f, -depth}),
                 local({0, half_h * 1.5f, -depth}), color, thickness);
      DrawLine3D(draw, local({0, half_h * 1.5f, -depth}),
                 local({half_w * 0.4f, half_h * 1.1f, -depth}), color, thickness);
    }
    drew_something = true;
  }

  if (Objects().GetProperty<DirectionalLight>(entity)) {
    ImVec2 at;
    if (WorldToScreen(position, at)) {
      const ImU32 color = selected ? selection_color : light_color;
      draw->AddCircle(at, 7.0f, color, 16, 2.0f);
      for (int i = 0; i < 8; i++) {
        const float angle = i * kPi / 4.0f;
        const ImVec2 d(std::cos(angle), std::sin(angle));
        draw->AddLine(ImVec2(at.x + d.x * 10, at.y + d.y * 10),
                      ImVec2(at.x + d.x * 14, at.y + d.y * 14), color, 1.5f);
      }
      // The way the light travels.
      const glm::vec3 direction = rotation * glm::vec3(0, 0, -1);
      DrawLine3D(draw, position, position + direction * 60.0f * world_per_pixel,
                 color, 1.5f);
    }
    drew_something = true;
  }

  glm::mat4 model;
  glm::vec3 min, max;
  if (selected && GetNodeBounds(entity, model, min, max)) {
    DrawBox3D(draw, model, min, max, selection_color, 2.0f);
  } else if (!drew_something && !node.IsRoot()) {
    // Empty nodes get a small marker so they can be seen and clicked.
    ImVec2 at;
    if (WorldToScreen(position, at)) {
      draw->AddCircle(at, selected ? 7.0f : 4.0f,
                      selected ? selection_color : Rgba(1, 1, 1, 0.5f), 12,
                      selected ? 2.0f : 1.0f);
    }
  }
}

void EditorScene::DrawAxisIndicator(ImDrawList* draw) {
  if (view_2d_) return;
  const ImVec2 center(scene_origin_.x + scene_size_.x - 46.0f,
                      scene_origin_.y + 50.0f);
  draw->AddCircleFilled(center, 36.0f, Rgba(0, 0, 0, 0.25f));
  const glm::quat to_view = glm::inverse(
      Transform::EulerToQuat(glm::vec3(camera_pitch_, camera_yaw_, 0.0f)));
  static const char* kLabels[3] = {"X", "Y", "Z"};
  glm::vec3 view_axes[3];
  for (int i = 0; i < 3; i++) view_axes[i] = to_view * kAxes[i];
  // Axes pointing away from the viewer are drawn first.
  int order[3] = {0, 1, 2};
  std::sort(order, order + 3,
            [&](int a, int b) { return view_axes[a].z < view_axes[b].z; });
  for (int axis : order) {
    const ImVec2 tip(center.x + view_axes[axis].x * 26.0f,
                     center.y - view_axes[axis].y * 26.0f);
    draw->AddLine(center, tip, AxisColor(axis, false), 2.0f);
    draw->AddCircleFilled(tip, 7.0f, AxisColor(axis, false));
    const ImVec2 text = ImGui::CalcTextSize(kLabels[axis]);
    draw->AddText(ImVec2(tip.x - text.x / 2, tip.y - text.y / 2),
                  Rgba(0, 0, 0, 0.9f), kLabels[axis]);
  }
}

// ---------------------------------------------------------- Camera input ---

void EditorScene::HandleSceneCamera(bool hovered, bool active) {
  ImGuiIO& io = ImGui::GetIO();
  const glm::quat orientation =
      view_2d_ ? glm::quat(1, 0, 0, 0)
               : Transform::EulerToQuat(glm::vec3(camera_pitch_, camera_yaw_, 0.0f));
  const glm::vec3 right = orientation * glm::vec3(1, 0, 0);
  const glm::vec3 up = orientation * glm::vec3(0, 1, 0);

  // Zoom: in 2D towards the cursor, in 3D towards the point looked at.
  if (hovered && io.MouseWheel != 0.0f) {
    if (view_2d_) {
      const Ray before = ScreenRay(io.MousePos);
      camera_distance_ *= std::pow(0.88f, io.MouseWheel);
      camera_distance_ = std::clamp(camera_distance_, 0.5f, 20000.0f);
      UpdateEditorCamera();
      const Ray after = ScreenRay(io.MousePos);
      auto a = RayPlane(before.origin, before.direction, glm::vec3(0), {0, 0, 1});
      auto b = RayPlane(after.origin, after.direction, glm::vec3(0), {0, 0, 1});
      if (a && b) camera_target_ += *a - *b;
    } else {
      camera_distance_ *= std::pow(0.85f, io.MouseWheel);
      camera_distance_ = std::clamp(camera_distance_, 0.05f, 100000.0f);
    }
  }

  // Pan: middle drag, or right drag in 2D.
  const bool pan = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
                   (view_2d_ && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f));
  if (active && pan) {
    const float world_per_pixel = WorldPerPixel(camera_target_);
    camera_target_ -= right * io.MouseDelta.x * world_per_pixel;
    camera_target_ += up * io.MouseDelta.y * world_per_pixel;
  }
  if (view_2d_) return;

  // Right drag: look around from where the camera is; with WASD/QE, fly.
  if (active && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    const glm::vec3 eye = CameraPosition();
    camera_yaw_ -= io.MouseDelta.x * 0.25f;
    camera_pitch_ =
        std::clamp(camera_pitch_ - io.MouseDelta.y * 0.25f, -89.0f, 89.0f);
    camera_target_ = eye + CameraForward() * camera_distance_;

    glm::vec3 move(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += CameraForward();
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= CameraForward();
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
    if (ImGui::IsKeyDown(ImGuiKey_E)) move += glm::vec3(0, 1, 0);
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= glm::vec3(0, 1, 0);
    if (move != glm::vec3(0.0f)) {
      const float speed =
          std::max(camera_distance_, 2.0f) * (io.KeyShift ? 3.0f : 1.0f);
      camera_target_ += glm::normalize(move) * speed * io.DeltaTime;
    }
  }

  // Alt + left drag: orbit around the point looked at.
  if (drag_ == Drag::kOrbit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    camera_yaw_ -= io.MouseDelta.x * 0.3f;
    camera_pitch_ =
        std::clamp(camera_pitch_ - io.MouseDelta.y * 0.3f, -89.0f, 89.0f);
  }
}

void EditorScene::FrameSelected() {
  MapNode* node = GetSelectedNode();
  if (!node) return;
  kEntityId entity = GetEntityForNode(node->uid);
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return;

  glm::vec3 center = transform->GetPosition();
  float radius = 0.5f;
  glm::mat4 model;
  glm::vec3 min, max;
  if (GetNodeBounds(entity, model, min, max)) {
    glm::vec3 world_min(std::numeric_limits<float>::max());
    glm::vec3 world_max(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; i++) {
      const glm::vec3 corner(i & 1 ? max.x : min.x, i & 2 ? max.y : min.y,
                             i & 4 ? max.z : min.z);
      const glm::vec3 world(model * glm::vec4(corner, 1.0f));
      world_min = glm::min(world_min, world);
      world_max = glm::max(world_max, world);
    }
    center = (world_min + world_max) * 0.5f;
    radius = std::max(glm::length(world_max - world_min) * 0.5f, 0.05f);
  }
  if (view_2d_) {
    // Recenter but keep the zoom, which suits 2D maps' scale.
    camera_target_ = glm::vec3(center.x, center.y, 0.0f);
    return;
  }
  camera_target_ = center;
  camera_distance_ = radius / std::sin(glm::radians(kEditorFov) / 2.0f) * 1.3f;
}

bool EditorScene::MapLooks3D() const {
  bool is_3d = false;
  const_cast<Map&>(map_).Visit([&](const MapNode& node) {
    for (const auto& component : node.components) {
      if (component.type == "MeshRenderer" ||
          component.type == "DirectionalLight" ||
          component.type == "SphereCollider" ||
          component.type == "CapsuleCollider" ||
          component.type == "CylinderCollider") {
        is_3d = true;
      }
      if (component.type == "Camera" &&
          component.properties.GetFloat("fit_height", 0.0) <= 0.0) {
        is_3d = true;
      }
    }
  });
  return is_3d;
}

void EditorScene::FrameMap() {
  // World positions come straight from the map data, since the scene is
  // only rebuilt at the end of the frame.
  glm::vec3 min(std::numeric_limits<float>::max());
  glm::vec3 max(std::numeric_limits<float>::lowest());
  float fit_height = 0.0f;
  glm::vec2 camera_center(0.0f);
  std::function<void(const MapNode&, glm::vec3)> visit =
      [&](const MapNode& node, glm::vec3 parent) {
        glm::vec3 position(0.0f);
        glm::vec3 scale(1.0f);
        GetVec3(node, "position", position);
        GetVec3(node, "scale", scale);
        position += parent;
        if (auto camera = node.FindComponent("Camera")) {
          const float fit =
              static_cast<float>(camera->properties.GetFloat("fit_height", 0.0));
          if (fit > 0.0f && fit_height == 0.0f) {
            fit_height = fit;
            camera_center = glm::vec2(position);
          }
        }
        // Cameras and lights usually sit away from the action.
        const bool helper = node.FindComponent("Camera") ||
                            node.FindComponent("DirectionalLight");
        if (!node.IsRoot() && !helper) {
          min = glm::min(min, position - glm::abs(scale) * 0.5f);
          max = glm::max(max, position + glm::abs(scale) * 0.5f);
        }
        for (const auto& child : node.children) visit(*child, position);
      };
  visit(*map_.GetRoot(), glm::vec3(0.0f));

  if (view_2d_) {
    // Framed like the game camera.
    camera_target_ = glm::vec3(camera_center, 0.0f);
    camera_distance_ = fit_height > 0.0f
                           ? fit_height * 1.15f / 2.0f /
                                 std::tan(glm::radians(kEditorFov) / 2.0f)
                           : 120.0f;
    return;
  }
  camera_yaw_ = 35.0f;
  camera_pitch_ = -25.0f;
  if (min.x > max.x) {  // an empty map
    camera_target_ = glm::vec3(0.0f);
    camera_distance_ = 12.0f;
    return;
  }
  camera_target_ = (min + max) * 0.5f;
  camera_distance_ =
      std::clamp(glm::length(max - min) * 0.8f, 6.0f, 10000.0f);
}

// ------------------------------------------------------------------ Panel ---

void EditorScene::DrawSceneToolbar() {
  ImGui::SetCursorScreenPos(ImVec2(scene_origin_.x + 8, scene_origin_.y + 8));
  auto toggle = [](const char* label, bool on, const char* tooltip) {
    if (on) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool clicked = ImGui::Button(label);
    if (on) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
  };
  if (toggle("Move", tool_ == Tool::kMove, "Move (W)")) tool_ = Tool::kMove;
  ImGui::SameLine(0, 2);
  if (toggle("Rotate", tool_ == Tool::kRotate, "Rotate (E)")) tool_ = Tool::kRotate;
  ImGui::SameLine(0, 2);
  if (toggle("Scale", tool_ == Tool::kScale, "Scale (R)")) tool_ = Tool::kScale;
  ImGui::SameLine(0, 12);
  if (toggle("2D", view_2d_, "Switch between the 2D and 3D view")) {
    view_2d_ = !view_2d_;
    if (view_2d_) {
      camera_target_.z = 0.0f;
    } else {
      camera_yaw_ = 35.0f;
      camera_pitch_ = -25.0f;
    }
  }
}

void EditorScene::DrawScene() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const bool visible = ImGui::Begin("Scene");
  ImGui::PopStyleVar();
  if (!visible) {
    ImGui::End();
    return;
  }

  ImVec2 size = ImGui::GetContentRegionAvail();
  size.x = std::max(size.x, 1.0f);
  size.y = std::max(size.y, 1.0f);
  scene_origin_ = ImGui::GetCursorScreenPos();
  scene_size_ = size;
  if (render_host_) {
    render_host_->SetSceneViewportSize(static_cast<int>(size.x),
                                       static_cast<int>(size.y));
  }

  // The toolbar is submitted before the viewport so its buttons get the
  // mouse, but drawn on a later channel so it shows on top of the scene.
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->ChannelsSplit(2);
  draw->ChannelsSetCurrent(1);
  DrawSceneToolbar();
  draw->ChannelsSetCurrent(0);
  ImGui::SetCursorScreenPos(scene_origin_);
  ImGui::InvisibleButton("viewport", size,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImVec2 corner(scene_origin_.x + size.x, scene_origin_.y + size.y);

  const uint64_t texture = render_host_ ? render_host_->GetSceneTexture() : 0;
  if (texture) {
    draw->AddImage(static_cast<ImTextureID>(texture), scene_origin_, corner);
  } else {
    draw->AddRectFilled(scene_origin_, corner, Rgba(0.1f, 0.1f, 0.1f, 1.0f));
  }

  ImGuiIO& io = ImGui::GetIO();
  // Tool shortcuts, unless the right button is flying the camera with WASD.
  const bool scene_focused = ImGui::IsWindowFocused() || ImGui::IsWindowHovered();
  if (scene_focused && !io.WantTextInput && !io.KeyCtrl &&
      !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) tool_ = Tool::kMove;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool_ = Tool::kRotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) tool_ = Tool::kScale;
  }

  HandleSceneCamera(hovered, active);
  UpdateEditorCamera();

  draw->PushClipRect(scene_origin_, corner, true);
  DrawGrid(draw);
  map_.Visit([&](const MapNode& node) { DrawNodeGizmos(draw, node); });
  DrawToolHandles(draw);

  // Left click: with Alt (3D) orbit; on a handle, drag it; anywhere else,
  // select what is under the mouse and drag it along the ground (3D) or the
  // z = 0 plane (2D).
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (io.KeyAlt && !view_2d_) {
      drag_ = Drag::kOrbit;
    } else if (!BeginHandleDrag(io.MousePos)) {
      MapNode* picked = PickNode(io.MousePos);
      Select(picked);
      drag_ = Drag::kNone;
      auto transform =
          picked ? Objects().GetProperty<Transform>(GetEntityForNode(picked->uid))
                 : nullptr;
      if (transform) {
        drag_origin_ = transform->GetPosition();
        drag_plane_normal_ = glm::vec3(0, 0, 1);
        if (!view_2d_) {
          // Looking along the ground, drag across the view instead.
          drag_plane_normal_ = std::abs(CameraForward().y) > 0.2f
                                   ? glm::vec3(0, 1, 0)
                                   : -CameraForward();
        }
        const Ray ray = ScreenRay(io.MousePos);
        if (auto hit = RayPlane(ray.origin, ray.direction, drag_origin_,
                                drag_plane_normal_)) {
          drag_ = Drag::kBody;
          drag_start_world_ = *hit;
          drag_start_local_ = glm::vec3(0.0f);
          GetVec3(*picked, "position", drag_start_local_);
        }
      }
    }
  }
  if (drag_ != Drag::kNone && drag_ != Drag::kOrbit && active &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left,
                             drag_ == Drag::kBody ? 3.0f : 0.0f)) {
    UpdateDrag(io.MousePos);
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag_ = Drag::kNone;

  DrawAxisIndicator(draw);
  const char* hint =
      view_2d_ ? "Drag: move   Ctrl: snap   Right/middle drag: pan   Scroll: zoom"
                 "   F: focus   W/E/R: move/rotate/scale"
               : "Right drag: look (+WASD/QE: fly)   Alt+drag: orbit   Middle drag:"
                 " pan   Scroll: zoom   F: focus   W/E/R: tools   Ctrl: snap";
  draw->AddText(ImVec2(scene_origin_.x + 8, corner.y - ImGui::GetTextLineHeight() - 6),
                Rgba(1, 1, 1, 0.45f), hint);
  if (!texture) {
    draw->AddText(ImVec2(scene_origin_.x + 8, scene_origin_.y + 40),
                  Rgba(1, 1, 1, 0.6f), "Starting renderer...");
  }
  draw->PopClipRect();
  draw->ChannelsMerge();
  ImGui::End();
}

}  // namespace Mercury
