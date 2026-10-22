//
// Created by smadhur on 4/22/2026.
//

#ifndef MERCURYENGINE_INPUT_MANAGER_H
#define MERCURYENGINE_INPUT_MANAGER_H
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "GLFW/glfw3.h"
#include "glm/vec2.hpp"

namespace Mercury {

using Key = int;

enum class MouseButton { kLeft, kRight, kMiddle };
enum KeyStatus { kJustPressed = 0, kContinuous = 1, kReleased = 2 };

/**
 * @brief One raw event from this frame, in the order it happened (used by
 * the UI, which needs typed text and every press, not just key states)
 */
struct InputEvent {
  enum class Type { kKey, kChar, kMouseButton, kScroll };
  Type type = Type::kKey;
  int key = 0;       // kKey: GLFW key code
  int action = 0;    // kKey / kMouseButton: GLFW_PRESS, GLFW_RELEASE, GLFW_REPEAT
  int button = 0;    // kMouseButton: 0 left, 1 right, 2 middle
  int mods = 0;      // GLFW_MOD_* bits
  char32_t codepoint = 0;    // kChar
  glm::vec2 scroll{0.0f};    // kScroll
};

/**
 * @brief Get inputs from application
 */
class InputManager {
 public:
  InputManager();
  void Initialize(GLFWwindow* window);
  bool IsPressed(Key k);
  bool IsPressed(MouseButton btn);
  bool IsJustPressed(Key k);
  bool IsJustPressed(MouseButton btn);

  glm::vec2 GetCursorPos();
  glm::vec2 GetCursorDelta();

  void HandleKeyCallback(GLFWwindow* window, int key, int scancode, int action,
                         int mods);

  void HandleCursorCallback(GLFWwindow* window, double xpos, double ypos);

  void HandleScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

  void HandleMouseButtonCallback(GLFWwindow* window, int button, int action,
                                 int mods);

  void HandleCharCallback(GLFWwindow* window, unsigned int codepoint);
  void HandleCursorEnterCallback(GLFWwindow* window, int entered);

  // Events since the last BeginFrame(); the application clears them before
  // polling each frame.
  const std::vector<InputEvent>& GetFrameEvents() const { return frame_events_; }
  void BeginFrame() { frame_events_.clear(); }

  bool IsCursorInWindow() const { return cursor_inside_; }
  // Framebuffer pixels per window coordinate (2 on most HiDPI displays).
  glm::vec2 GetFramebufferScale() const { return framebuffer_scale_; }
  void SetFramebufferScale(glm::vec2 scale) { framebuffer_scale_ = scale; }
  // The monitor's content scale (1 = 96 DPI).
  float GetContentScale() const { return content_scale_; }
  void SetContentScale(float scale) { content_scale_ = scale; }

  // Whether Dear ImGui (dev/debug windows) wants the mouse or keyboard this
  // frame; the in-game UI ignores input while it does.
  void SetImGuiCapture(bool mouse, bool keyboard) {
    imgui_wants_mouse_ = mouse;
    imgui_wants_keyboard_ = keyboard;
  }
  bool ImGuiWantsMouse() const { return imgui_wants_mouse_; }
  bool ImGuiWantsKeyboard() const { return imgui_wants_keyboard_; }

  static std::string GetClipboard();
  static void SetClipboard(const std::string& text);

  void ResetFreshPresses();

  float GetScroll();

  bool IsHoveringOnUI();
  void SetHoveringOverUI(bool new_value);

  /**
   * @brief Key code for a name such as "W", "Space", "Up" or "Escape"
   *
   * Case-insensitive. Returns GLFW_KEY_UNKNOWN for names it does not know.
   */
  static Key KeyFromName(std::string_view name);

 private:
  void SetKeyRepeat(Key k);
  void SetKeyDown(Key k);
  void SetKeyUp(Key k);
  std::map<int, bool> key_status_ = {};
  std::map<MouseButton, KeyStatus> mouse_btn_status_ = {};
  std::map<MouseButton, KeyStatus> mouse_btn_fresh_status_ = {};
  std::map<int, bool> repeated_keys_ = {};
  std::map<int, bool> fresh_presses_ = {};
  glm::vec2 mouse_position_ = glm::vec2(0.0);
  glm::vec2 scroll_offset_ = glm::vec2(0.0);
  bool is_hovering_on_ui_ = false;
  glm::vec2 last_mouse_position_;
  std::vector<InputEvent> frame_events_;
  bool cursor_inside_ = true;
  glm::vec2 framebuffer_scale_ = glm::vec2(1.0f);
  float content_scale_ = 1.0f;
  bool imgui_wants_mouse_ = false;
  bool imgui_wants_keyboard_ = false;
};

}  // namespace Mercury

#endif  // MERCURYENGINE_INPUT_MANAGER_H
