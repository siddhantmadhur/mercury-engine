//
// Created by smadhur on 5/10/2026.
//

#include "scene.h"

#include <filesystem>
#include <format>
#include <debugger/debugger.h>

#include "imgui.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "os/filesystem.h"

constexpr int icon_size = 200;

namespace Pequod {
void UIEditorScene::OnDestroy() {}
void UIEditorScene::OnFrame(double delta_t) {
  ImGui::Begin("9-Slice");

  ImGui::SliderFloat("Horizontal##2", &horizontal_ratio, 0.0, 1.0);
  ImGui::SliderFloat("Vertical##1", &vertical_ratio, 0.0, 1.0);

  ImGui::End();
}
void UIEditorScene::OnStart() {
  {
    auto button = object_manager_->NewBox2D(glm::vec2(0.0), glm::vec2(200, 200),
                                            glm::vec4(1.0));
    auto file_path =
        fs::path(PEQUOD_EDITOR_ASSET_PATH) / "button_square_flat.png";
    auto tex = Texture2D(file_path.string());
    object_manager_->AddProperty(button, tex);
  }

  {
    this->horizontal_slice_left = object_manager_->NewBox2D(
        glm::vec2(-horizontal_ratio * (icon_size / 2.0), 0),
        glm::vec2(1, icon_size), glm::vec4(glm::vec3(1), 0.3));
    this->horizontal_slice_right = object_manager_->NewBox2D(
        glm::vec2(horizontal_ratio * (icon_size / 2.0), 0),
        glm::vec2(1, icon_size), glm::vec4(glm::vec3(1), 0.3));
    this->vertical_slice_left = object_manager_->NewBox2D(
        glm::vec2(0, -vertical_ratio * (icon_size / 2.0)),
        glm::vec2(icon_size, 1), glm::vec4(glm::vec3(1), 0.3));
    this->vertical_slice_right = object_manager_->NewBox2D(
        glm::vec2(0, vertical_ratio * (icon_size / 2.0)),
        glm::vec2(icon_size, 1), glm::vec4(glm::vec3(1), 0.3));
  }
}
void UIEditorScene::OnTick(double delta_t) {
  if (input_manager_->IsPressed(GLFW_KEY_LEFT_CONTROL) &&
      input_manager_->IsPressed(GLFW_KEY_Q)) {
    QuitScene();
  }

  auto scroll = input_manager_->GetScroll();
  if (scroll != 0) {
    if (scroll < 0) {
      camera_zoom = glm::max(1.0, camera_zoom * 0.9);
    }
    if (scroll > 0) {
      camera_zoom = glm::min(100.0, camera_zoom * 1.1);
    }

    auto cam = object_manager_->GetProperty<Camera>(player_camera_);
    cam->SetZoom(camera_zoom);
  }

  {
    auto transform =
        object_manager_->GetProperty<Transform>(horizontal_slice_left);
    auto correct_x = -horizontal_ratio * (icon_size / 2.0);
    transform->SetPosition(glm::vec3(correct_x, 0, 0));
  }
  {
    auto transform =
        object_manager_->GetProperty<Transform>(horizontal_slice_right);
    auto correct_x = horizontal_ratio * (icon_size / 2.0);
    transform->SetPosition(glm::vec3(correct_x, 0, 0));
  }
  {
    auto transform =
        object_manager_->GetProperty<Transform>(vertical_slice_left);
    auto correct_y = -vertical_ratio * (icon_size / 2.0);
    transform->SetPosition(glm::vec3(0, correct_y, 0));
  }
  {
    auto transform =
        object_manager_->GetProperty<Transform>(vertical_slice_right);
    auto correct_y = vertical_ratio * (icon_size / 2.0);
    transform->SetPosition(glm::vec3(0, correct_y, 0));
  }
}

}  // namespace Pequod