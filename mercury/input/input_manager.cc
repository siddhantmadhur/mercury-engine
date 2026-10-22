//
// Created by smadhur on 4/22/2026.
//

#include "input_manager.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Mercury {
InputManager::InputManager() {}

Key InputManager::KeyFromName(std::string_view name) {
  std::string key(name);
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (key.size() == 1) {
    char c = key[0];
    if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
  }
  if (key.size() >= 2 && key[0] == 'F') {
    int n = std::atoi(key.c_str() + 1);
    if (n >= 1 && n <= 25) return GLFW_KEY_F1 + (n - 1);
  }
  static const std::map<std::string, Key> kNamed = {
      {"SPACE", GLFW_KEY_SPACE},          {"ESCAPE", GLFW_KEY_ESCAPE},
      {"ESC", GLFW_KEY_ESCAPE},           {"ENTER", GLFW_KEY_ENTER},
      {"RETURN", GLFW_KEY_ENTER},         {"TAB", GLFW_KEY_TAB},
      {"BACKSPACE", GLFW_KEY_BACKSPACE},  {"UP", GLFW_KEY_UP},
      {"DOWN", GLFW_KEY_DOWN},            {"LEFT", GLFW_KEY_LEFT},
      {"RIGHT", GLFW_KEY_RIGHT},          {"SHIFT", GLFW_KEY_LEFT_SHIFT},
      {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT},
      {"CTRL", GLFW_KEY_LEFT_CONTROL},    {"LEFT_CTRL", GLFW_KEY_LEFT_CONTROL},
      {"RIGHT_CTRL", GLFW_KEY_RIGHT_CONTROL}, {"ALT", GLFW_KEY_LEFT_ALT},
  };
  auto it = kNamed.find(key);
  return it == kNamed.end() ? GLFW_KEY_UNKNOWN : it->second;
}
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
    return mouse_btn_status_[btn] == kJustPressed ||
           mouse_btn_status_[btn] == kContinuous;
  }
  return false;
}
bool InputManager::IsJustPressed(MouseButton btn) {
  if (mouse_btn_fresh_status_.contains(btn)) {
    return mouse_btn_fresh_status_[btn] == kJustPressed;
  }
  return false;
}

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
  mouse_btn_fresh_status_.clear();
  this->scroll_offset_ = glm::vec2(0.0f);
  this->last_mouse_position_ = GetCursorPos();
}
void InputManager::HandleKeyCallback(GLFWwindow* window, int key, int scancode,
                                     int action, int mods) {
  InputEvent event;
  event.type = InputEvent::Type::kKey;
  event.key = key;
  event.action = action;
  event.mods = mods;
  frame_events_.push_back(event);
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
  InputEvent event;
  event.type = InputEvent::Type::kScroll;
  event.scroll = glm::vec2(xoffset, yoffset);
  frame_events_.push_back(event);
}

void InputManager::HandleCharCallback(GLFWwindow* window, unsigned int codepoint) {
  InputEvent event;
  event.type = InputEvent::Type::kChar;
  event.codepoint = static_cast<char32_t>(codepoint);
  frame_events_.push_back(event);
}

void InputManager::HandleCursorEnterCallback(GLFWwindow* window, int entered) {
  cursor_inside_ = entered != 0;
}

std::string InputManager::GetClipboard() {
  const char* text = glfwGetClipboardString(nullptr);
  return text ? text : "";
}

void InputManager::SetClipboard(const std::string& text) {
  glfwSetClipboardString(nullptr, text.c_str());
}
#define GET_ACTION(a) \
  a == GLFW_PRESS ? kJustPressed : a == GLFW_REPEAT ? kContinuous : kReleased
void InputManager::HandleMouseButtonCallback(GLFWwindow* window, int button,
                                             int action, int mods) {
  if (button <= GLFW_MOUSE_BUTTON_MIDDLE) {
    InputEvent event;
    event.type = InputEvent::Type::kMouseButton;
    // GLFW numbers buttons left, right, middle.
    event.button = button;
    event.action = action;
    event.mods = mods;
    frame_events_.push_back(event);
  }
  switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      mouse_btn_status_[MouseButton::kLeft] = GET_ACTION(action);
      mouse_btn_fresh_status_[MouseButton::kLeft] = GET_ACTION(action);
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      mouse_btn_status_[MouseButton::kRight] = GET_ACTION(action);
      mouse_btn_fresh_status_[MouseButton::kRight] = GET_ACTION(action);
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      mouse_btn_status_[MouseButton::kMiddle] = GET_ACTION(action);
      mouse_btn_fresh_status_[MouseButton::kMiddle] = GET_ACTION(action);
      break;
    default:
      break;
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

}  // namespace Mercury
