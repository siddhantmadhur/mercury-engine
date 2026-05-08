//
// Created by smadhur on 4/22/2026.
//

#ifndef PEQUODENGINE_INPUT_MANAGER_H
#define PEQUODENGINE_INPUT_MANAGER_H
#include <map>

#include "GLFW/glfw3.h"
#include "glm/vec2.hpp"

namespace Pequod {

using Key = int;

/**
 * @brief Get inputs from application
 */
class InputManager {
 public:
  InputManager();
  void Initialize(GLFWwindow* window);
  bool IsPressed(Key k);
  bool IsJustPressed(Key k);
  glm::vec2 GetCursorPos();

  void HandleKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                         int mods);

  void HandleCursorCallback(GLFWwindow* window, double xpos, double ypos);

  void HandleScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

  void ResetFreshPresses();

  float GetScroll();

  bool IsHoveringOnUI();
  void SetHoveringOverUI(bool new_value);

 private:
  void SetKeyRepeat(Key k);
  void SetKeyDown(Key k);
  void SetKeyUp(Key k);
  std::map<int, bool> key_status_ = {};
  std::map<int, bool> repeated_keys_ = {};
  std::map<int, bool> fresh_presses_ = {};
  glm::vec2 mouse_position_ = glm::vec2(0.0);
  glm::vec2 scroll_offset_ = glm::vec2(0.0);
  bool is_hovering_on_ui_ = false;
};

}  // namespace Pequod

#endif  // PEQUODENGINE_INPUT_MANAGER_H
