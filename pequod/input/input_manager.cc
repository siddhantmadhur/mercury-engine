//
// Created by smadhur on 4/22/2026.
//

#include "input_manager.h"

#include "imgui.h"

namespace Pequod {
InputManager::InputManager() {}
bool InputManager::IsPressed(Key k) {
  if (key_status_.contains(k)) {
    return key_status_[k];
  } else {
    return false;
  }
}
bool InputManager::IsJustPressed(Key k) {
  if (fresh_presses_.contains(k)) {
    return fresh_presses_[k];
  } else {
    return false;
  }
}

bool InputManager::IsPressed(MouseButton btn) {
  if (mouse_btn_status_.contains(btn)) {
    return mouse_btn_status_[btn] == kJustPressed;
  }
  return false;
}
bool InputManager::IsJustPressed(MouseButton btn) { return IsPressed(btn); }

glm::vec2 InputManager::GetCursorDelta() {
  return this->mouse_position_ - this->last_mouse_position_;
}
glm::vec2 InputManager::GetCursorPos() { return this->mouse_position_; }
void InputManager::SetKeyDown(Key k) {
  key_status_[k] = true;
  fresh_presses_[k] = true;
}
void InputManager::SetKeyUp(Key k) {
  key_status_[k] = false;
  repeated_keys_[k] = false;
  fresh_presses_[k] = false;
}
void InputManager::ResetFreshPresses() {
  fresh_presses_.clear();
  this->scroll_offset_ = glm::vec2(0.0f);
  this->last_mouse_position_ = GetCursorPos();
}
void InputManager::HandleKeyCallback(GLFWwindow* window, int key, int scancode,
                                     int action, int mods) {
  if (action == GLFW_PRESS) {
    SetKeyDown(key);
  } else if (action == GLFW_RELEASE) {
    SetKeyUp(key);
  } else if (action == GLFW_REPEAT) {
    SetKeyRepeat(key);
  }
}
void InputManager::HandleCursorCallback(GLFWwindow* window, double xpos,
                                        double ypos) {
  this->mouse_position_ = glm::vec2(xpos, ypos);
}
void InputManager::HandleScrollCallback(GLFWwindow* window, double xoffset,
                                        double yoffset) {
  this->scroll_offset_ = glm::vec2(xoffset, yoffset);
}
void InputManager::HandleMouseButtonCallback(GLFWwindow* window, int button,
                                             int action, int mods) {
  if (action == GLFW_PRESS) {
    switch (button) {
      case GLFW_MOUSE_BUTTON_LEFT:
        mouse_btn_status_[MouseButton::kLeft] = kJustPressed;
        break;
    }
  } else if (action == GLFW_RELEASE) {
    switch (button) {
      case GLFW_MOUSE_BUTTON_LEFT:
        mouse_btn_status_[MouseButton::kLeft] = kReleased;
        break;
    }
  }
}

float InputManager::GetScroll() { return this->scroll_offset_.y; }
void InputManager::SetHoveringOverUI(bool new_value) {
  this->is_hovering_on_ui_ = new_value;
}
bool InputManager::IsHoveringOnUI() {
  bool is_hovering = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
  return is_hovering;
}
void InputManager::SetKeyRepeat(Key k) { repeated_keys_[k] = true; }

}  // namespace Pequod
