// The editor's Scene panel: the map rendered offscreen, an editor camera
// that pans and zooms over the XY plane, click-to-select, drag-to-move and
// gizmos for things that have no sprite (colliders, cameras, empty nodes).

#include <cmath>
#include <limits>

#include "editor_scene.h"
#include "properties/mesh.h"

namespace Mercury {

namespace {

constexpr float kEditorFov = 60.0f;
constexpr float kPickRadiusPixels = 7.0f;

ImU32 Rgba(float r, float g, float b, float a) {
  return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

float DistanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
  glm::vec2 ab = b - a;
  float t = glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f);
  t = std::clamp(t, 0.0f, 1.0f);
  return glm::length(p - (a + ab * t));
}

float DistanceToRectBorder(glm::vec2 p, glm::vec2 min, glm::vec2 max) {
  glm::vec2 corners[4] = {min, {max.x, min.y}, max, {min.x, max.y}};
  float best = std::numeric_limits<float>::max();
  for (int i = 0; i < 4; i++) {
    best = std::min(best, DistanceToSegment(p, corners[i], corners[(i + 1) % 4]));
  }
  return best;
}

}  // namespace

void EditorScene::UpdateEditorCamera() {
  auto transform = Objects().GetProperty<Transform>(GetDefaultCamera());
  if (!transform) return;
  // Straight down -Z, like a 2D camera (+X right, +Y up).
  transform->SetPosition(glm::vec3(camera_target_.x, camera_target_.y,
                                   camera_target_.z + camera_distance_));
  transform->SetRotate(glm::vec3(0.0f, -90.0f, 0.0f));
  transform->SnapInterpolation();
  transform->ClearTransformations();
}

glm::mat4 EditorScene::GetViewProjection() const {
  glm::mat4 view_projection(1.0f);
  const_cast<EditorScene*>(this)->GetCameraProj(view_projection);
  return view_projection;
}

std::optional<glm::vec3> EditorScene::ScreenToWorld(ImVec2 pixel) const {
  if (scene_size_.x <= 0 || scene_size_.y <= 0) return std::nullopt;
  const float half_height =
      camera_distance_ * std::tan(glm::radians(kEditorFov) / 2.0f);
  const float aspect = scene_size_.x / scene_size_.y;
  const float ndc_x = (pixel.x - scene_origin_.x) / scene_size_.x * 2.0f - 1.0f;
  const float ndc_y = 1.0f - (pixel.y - scene_origin_.y) / scene_size_.y * 2.0f;
  return glm::vec3(camera_target_.x + ndc_x * half_height * aspect,
                   camera_target_.y + ndc_y * half_height, 0.0f);
}

ImVec2 EditorScene::WorldToScreen(glm::vec3 world) const {
  const float half_height =
      camera_distance_ * std::tan(glm::radians(kEditorFov) / 2.0f);
  const float aspect = scene_size_.x / std::max(scene_size_.y, 1.0f);
  // Points off the z = 0 plane shrink or grow with perspective.
  const float depth = std::max(camera_distance_ - world.z, 1e-3f);
  const float scale = camera_distance_ / depth;
  const float ndc_x = (world.x - camera_target_.x) * scale / (half_height * aspect);
  const float ndc_y = (world.y - camera_target_.y) * scale / half_height;
  return ImVec2(scene_origin_.x + (ndc_x + 1.0f) * 0.5f * scene_size_.x,
                scene_origin_.y + (1.0f - ndc_y) * 0.5f * scene_size_.y);
}

bool EditorScene::GetNodeBounds(kEntityId entity, glm::vec3& min,
                                glm::vec3& max) {
  auto transform = Objects().GetProperty<Transform>(entity);
  if (!transform) return false;
  const glm::vec3 position = transform->GetPosition();
  const glm::vec3 scale = transform->GetScale();
  glm::vec3 half(0.0f);
  if (Objects().GetProperty<Mesh>(entity)) {
    half = glm::abs(scale) * 0.5f;
  } else if (auto box = Objects().GetProperty<BoxCollider>(entity)) {
    half = glm::abs(box->size * scale) * 0.5f;
  } else {
    return false;
  }
  min = position - half;
  max = position + half;
  return true;
}

MapNode* EditorScene::PickNode(glm::vec3 point) {
  const float world_per_pixel =
      2.0f * camera_distance_ * std::tan(glm::radians(kEditorFov) / 2.0f) /
      std::max(scene_size_.y, 1.0f);
  const float radius = kPickRadiusPixels * world_per_pixel;
  const glm::vec2 p(point);

  // Lower tier wins: things with bounds, then empty-node markers, then wall
  // planes, then camera frames. Within a tier the lower score wins.
  enum Tier { kBounds, kMarker, kPlane, kCameraFrame };
  MapNode* best = nullptr;
  std::pair<int, float> best_rank = {std::numeric_limits<int>::max(), 0.0f};
  auto consider = [&](MapNode& node, Tier tier, float score) {
    std::pair<int, float> rank = {tier, score};
    if (rank < best_rank) {
      best_rank = rank;
      best = &node;
    }
  };

  map_.Visit([&](MapNode& node) {
    if (node.IsRoot()) return;
    kEntityId entity = GetEntityForNode(node.uid);
    auto transform = Objects().GetProperty<Transform>(entity);
    if (!transform) return;
    const glm::vec2 position(transform->GetPosition());

    glm::vec3 min, max;
    if (GetNodeBounds(entity, min, max)) {
      // Smaller objects win, so the ball can be picked in front of a wall.
      if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y) {
        consider(node, kBounds, (max.x - min.x) * (max.y - min.y));
      }
      return;
    }
    if (auto plane = Objects().GetProperty<PlaneCollider>(entity)) {
      glm::vec2 normal(plane->normal);
      if (glm::length(normal) > 1e-4f) {
        glm::vec2 along = glm::normalize(glm::vec2(-normal.y, normal.x));
        const float distance = std::abs(glm::dot(p - position,
                                                 glm::vec2(-along.y, along.x)));
        if (distance < radius) consider(node, kPlane, distance);
        return;
      }
    }
    if (auto camera = Objects().GetProperty<CameraComponent>(entity);
        camera && camera->fit_height > 0.0f) {
      const glm::vec2 half(camera->fit_height * game_aspect_ * 0.5f,
                           camera->fit_height * 0.5f);
      if (DistanceToRectBorder(p, position - half, position + half) < radius) {
        consider(node, kCameraFrame, 0.0f);
      }
      return;
    }
    const float distance = glm::length(p - position);
    if (distance < radius * 1.5f) consider(node, kMarker, distance);
  });
  return best;
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

  ImGui::InvisibleButton("viewport", size,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 corner(scene_origin_.x + size.x, scene_origin_.y + size.y);

  const uint64_t texture = render_host_ ? render_host_->GetSceneTexture() : 0;
  if (texture) {
    draw->AddImage(static_cast<ImTextureID>(texture), scene_origin_, corner);
  } else {
    draw->AddRectFilled(scene_origin_, corner, Rgba(0.1f, 0.1f, 0.1f, 1.0f));
  }

  ImGuiIO& io = ImGui::GetIO();
  const float world_per_pixel =
      2.0f * camera_distance_ * std::tan(glm::radians(kEditorFov) / 2.0f) /
      size.y;

  // Zoom towards the cursor.
  if (hovered && io.MouseWheel != 0.0f) {
    auto before = ScreenToWorld(io.MousePos);
    camera_distance_ *= std::pow(0.88f, io.MouseWheel);
    camera_distance_ = std::clamp(camera_distance_, 2.0f, 20000.0f);
    auto after = ScreenToWorld(io.MousePos);
    if (before && after) camera_target_ += *before - *after;
  }
  // Pan with the right or middle button.
  if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
    camera_target_.x -= io.MouseDelta.x * world_per_pixel;
    camera_target_.y += io.MouseDelta.y * world_per_pixel;
  }

  // Select, and start dragging the selection.
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (auto world = ScreenToWorld(io.MousePos)) {
      MapNode* picked = PickNode(*world);
      Select(picked);
      dragging_node_ = picked != nullptr;
      if (picked) {
        drag_start_world_ = *world;
        drag_start_local_ = glm::vec3(0.0f);
        if (auto transform = picked->FindComponent("Transform")) {
          if (auto value = transform->properties.Get("position")) {
            if (auto v = value->Get<glm::vec3>()) drag_start_local_ = *v;
          }
        }
      }
    }
  }
  if (dragging_node_ && active &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
    MapNode* node = GetSelectedNode();
    auto world = ScreenToWorld(io.MousePos);
    if (node && world) {
      glm::vec3 position = drag_start_local_ + glm::vec3(glm::vec2(*world - drag_start_world_), 0.0f);
      if (io.KeyCtrl) {
        position.x = std::round(position.x);
        position.y = std::round(position.y);
      }
      ComponentData* transform = node->FindComponent("Transform");
      if (!transform) {
        node->components.insert(node->components.begin(), {"Transform", {}});
        transform = &node->components.front();
      }
      const mtext::Variant* current = transform->properties.Get("position");
      if (!current || !(*current == mtext::Variant(position))) {
        transform->properties.Set("position", position);
        MarkChanged();
      }
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_node_ = false;

  draw->PushClipRect(scene_origin_, corner, true);
  DrawSceneGizmos(draw, scene_origin_, size);

  const char* hint =
      "Drag: move   Ctrl: snap   Right/middle drag: pan   Scroll: zoom   F: focus";
  draw->AddText(ImVec2(scene_origin_.x + 8, corner.y - ImGui::GetTextLineHeight() - 6),
                Rgba(1, 1, 1, 0.45f), hint);
  if (!texture) {
    draw->AddText(ImVec2(scene_origin_.x + 8, scene_origin_.y + 8),
                  Rgba(1, 1, 1, 0.6f), "Starting renderer...");
  }
  draw->PopClipRect();
  ImGui::End();
}

void EditorScene::DrawSceneGizmos(ImDrawList* draw, ImVec2 origin, ImVec2 size) {
  const float world_per_pixel =
      2.0f * camera_distance_ * std::tan(glm::radians(kEditorFov) / 2.0f) /
      size.y;

  // Grid: lines every power of ten that keeps them at least ~40px apart.
  {
    const float spacing = std::pow(10.0f, std::ceil(std::log10(40.0f * world_per_pixel)));
    auto top_left = ScreenToWorld(origin);
    auto bottom_right = ScreenToWorld(ImVec2(origin.x + size.x, origin.y + size.y));
    if (top_left && bottom_right && spacing > 0.0f) {
      const ImU32 minor = Rgba(1, 1, 1, 0.05f);
      for (float x = std::floor(top_left->x / spacing) * spacing; x <= bottom_right->x; x += spacing) {
        draw->AddLine(WorldToScreen({x, top_left->y, 0}), WorldToScreen({x, bottom_right->y, 0}), minor);
      }
      for (float y = std::floor(bottom_right->y / spacing) * spacing; y <= top_left->y; y += spacing) {
        draw->AddLine(WorldToScreen({top_left->x, y, 0}), WorldToScreen({bottom_right->x, y, 0}), minor);
      }
      // Axes through the origin: X red, Y green.
      draw->AddLine(WorldToScreen({top_left->x, 0, 0}), WorldToScreen({bottom_right->x, 0, 0}),
                    Rgba(0.9f, 0.3f, 0.3f, 0.35f));
      draw->AddLine(WorldToScreen({0, top_left->y, 0}), WorldToScreen({0, bottom_right->y, 0}),
                    Rgba(0.3f, 0.9f, 0.3f, 0.35f));
    }
  }

  const ImU32 collider_color = Rgba(0.35f, 0.95f, 0.45f, 0.65f);
  const ImU32 camera_color = Rgba(0.5f, 0.8f, 1.0f, 0.8f);
  const ImU32 selection_color = Rgba(1.0f, 0.6f, 0.1f, 1.0f);

  map_.Visit([&](const MapNode& node) {
    kEntityId entity = GetEntityForNode(node.uid);
    auto transform = Objects().GetProperty<Transform>(entity);
    if (!transform) return;
    const glm::vec3 position = transform->GetPosition();
    const glm::vec3 flat(position.x, position.y, 0.0f);
    const bool selected = node.uid == selected_uid_;
    bool drew_something = Objects().GetProperty<Mesh>(entity) != nullptr;

    if (auto plane = Objects().GetProperty<PlaneCollider>(entity)) {
      glm::vec2 normal(plane->normal);
      if (glm::length(normal) > 1e-4f) {
        normal = glm::normalize(normal);
        const glm::vec2 along(-normal.y, normal.x);
        const float reach = 100000.0f;
        draw->AddLine(WorldToScreen(flat - glm::vec3(along * reach, 0)),
                      WorldToScreen(flat + glm::vec3(along * reach, 0)),
                      selected ? selection_color : collider_color,
                      selected ? 2.5f : 1.5f);
        // Normal arrow, 24px long.
        const ImVec2 from = WorldToScreen(flat);
        const ImVec2 to = WorldToScreen(flat + glm::vec3(normal * 24.0f * world_per_pixel, 0));
        draw->AddLine(from, to, collider_color, 1.5f);
        draw->AddCircleFilled(to, 3.0f, collider_color);
        drew_something = true;
      }
    }
    if (auto box = Objects().GetProperty<BoxCollider>(entity)) {
      const glm::vec3 half = glm::abs(box->size * transform->GetScale()) * 0.5f;
      draw->AddRect(WorldToScreen({flat.x - half.x, flat.y + half.y, 0}),
                    WorldToScreen({flat.x + half.x, flat.y - half.y, 0}),
                    collider_color, 0.0f, 0, 1.0f);
      drew_something = true;
    }
    if (auto camera = Objects().GetProperty<CameraComponent>(entity);
        camera && camera->fit_height > 0.0f) {
      const float half_h = camera->fit_height * 0.5f;
      const float half_w = half_h * game_aspect_;
      const ImVec2 min = WorldToScreen({flat.x - half_w, flat.y + half_h, 0});
      const ImVec2 max = WorldToScreen({flat.x + half_w, flat.y - half_h, 0});
      draw->AddRect(min, max, selected ? selection_color : camera_color, 0.0f, 0,
                    selected ? 2.5f : 1.5f);
      draw->AddText(ImVec2(min.x + 4, min.y + 2), camera_color,
                    node.name.c_str());
      drew_something = true;
    }

    glm::vec3 min, max;
    if (selected && GetNodeBounds(entity, min, max)) {
      ImVec2 a = WorldToScreen({min.x, max.y, 0});
      ImVec2 b = WorldToScreen({max.x, min.y, 0});
      draw->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2),
                    selection_color, 0.0f, 0, 2.0f);
    } else if (!drew_something && !node.IsRoot()) {
      // Empty nodes get a small marker so they can be seen and clicked.
      const ImVec2 at = WorldToScreen(flat);
      draw->AddCircle(at, selected ? 7.0f : 4.0f,
                      selected ? selection_color : Rgba(1, 1, 1, 0.5f), 12,
                      selected ? 2.0f : 1.0f);
    }
  });
}

}  // namespace Mercury
