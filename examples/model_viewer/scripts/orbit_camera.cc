#include "orbit_camera.h"

#include <algorithm>
#include <cmath>

#include "input/input_manager.h"
#include "properties/render_mesh.h"

MERCURY_SCRIPT(OrbitCamera);

void OrbitCamera::OnStart() {
  if (distance <= 0.0f) {
    // Frame the target's mesh: far enough for its bounding sphere to fit.
    distance = 5.0f;
    if (auto object = Get(target)) {
      if (auto* mesh = object.GetComponent<Mercury::RenderMesh>()) {
        const glm::vec3 scale = object.GetTransform()->GetScale();
        const glm::vec3 lo = mesh->bounds_min * scale, hi = mesh->bounds_max * scale;
        focus = object.GetTransform()->GetPosition() + (lo + hi) * 0.5f;
        distance = std::max(glm::length(hi - lo) * 1.1f, 0.1f);
      }
    }
  }
  start_focus_ = focus;
  start_distance_ = distance;
  start_yaw_ = yaw;
  start_pitch_ = pitch;
  Apply();
}

void OrbitCamera::OnFrame(double dt) {
  // The wheel is reported per frame; ticks may run less often.
  if (auto* input = GetScene().Input(); input && !IsPointerOverUI()) {
    scroll_ += input->GetFrameScroll().y;
  }
}

void OrbitCamera::OnTick(double dt) {
  Mercury::InputManager* input = GetScene().Input();
  if (!input) return;
  const glm::vec2 delta = input->GetCursorDelta();
  const float scroll = scroll_;
  scroll_ = 0.0f;
  const bool over_ui = IsPointerOverUI();
  bool moved = false;

  if (!over_ui && input->IsPressed(Mercury::MouseButton::kLeft)) {
    yaw -= delta.x * 0.3f;
    pitch = std::clamp(pitch + delta.y * 0.3f, -89.0f, 89.0f);
    moved = delta != glm::vec2(0.0f);
  }
  if (!over_ui && (input->IsPressed(Mercury::MouseButton::kRight) ||
                   input->IsPressed(Mercury::MouseButton::kMiddle))) {
    // Pan in the view plane, at a speed that keeps the focus under the mouse.
    const glm::quat rotation = Mercury::Transform::EulerToQuat({-pitch, yaw, 0.0f});
    const float speed = distance * 0.0015f;
    focus += rotation * glm::vec3(-delta.x * speed, delta.y * speed, 0.0f);
    moved = delta != glm::vec2(0.0f);
  }
  if (scroll != 0.0f) {
    distance = std::max(distance * std::pow(0.9f, scroll), 0.01f);
    moved = true;
  }
  if (IsKeyJustPressed(GLFW_KEY_R)) {
    focus = start_focus_;
    distance = start_distance_;
    yaw = start_yaw_;
    pitch = start_pitch_;
    moved = true;
  }

  idle_ = moved ? 0.0f : idle_ + static_cast<float>(dt);
  // Turn slowly on its own after a few seconds without input.
  if (idle_ > 3.0f) yaw += auto_rotate * static_cast<float>(dt);
  Apply();
}

void OrbitCamera::Apply() {
  // Looking down at the focus from `pitch` degrees above the horizon.
  const glm::quat rotation = Mercury::Transform::EulerToQuat({-pitch, yaw, 0.0f});
  GetTransform().SetPosition(focus + rotation * glm::vec3(0.0f, 0.0f, distance));
  GetTransform().SetRotate(glm::vec3(-pitch, yaw, 0.0f));
}
