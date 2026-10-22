/*
 * input.h
 *
 * Raw input handed to a UI panel each frame. The game fills it from the
 * InputManager; the editor's UI Builder fills it from its preview canvas.
 * Key codes use the same numbers as GLFW so converting is a cast.
 */
#ifndef MERCURY_UI_INPUT_H
#define MERCURY_UI_INPUT_H

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Mercury::UI {

enum class KeyCode : int {
  kNone = -1,
  kSpace = 32,
  kA = 65,
  kC = 67,
  kV = 86,
  kX = 88,
  kY = 89,
  kZ = 90,
  kEscape = 256,
  kEnter = 257,
  kTab = 258,
  kBackspace = 259,
  kInsert = 260,
  kDelete = 261,
  kRight = 262,
  kLeft = 263,
  kDown = 264,
  kUp = 265,
  kPageUp = 266,
  kPageDown = 267,
  kHome = 268,
  kEnd = 269,
  kKeypadEnter = 335,
};

using KeyModifiers = int;
constexpr KeyModifiers kModNone = 0;
constexpr KeyModifiers kModShift = 0x1;    // GLFW_MOD_SHIFT
constexpr KeyModifiers kModControl = 0x2;  // GLFW_MOD_CONTROL
constexpr KeyModifiers kModAlt = 0x4;      // GLFW_MOD_ALT
constexpr KeyModifiers kModSuper = 0x8;    // GLFW_MOD_SUPER

struct UIInputEvent {
  enum class Type : uint8_t {
    kPointerDown,
    kPointerUp,
    kWheel,
    kKeyDown,  // also key repeats
    kKeyUp,
    kChar,
  };
  Type type = Type::kPointerDown;
  int button = 0;  // 0 left, 1 right, 2 middle
  KeyCode key = KeyCode::kNone;
  char32_t character = 0;
  glm::vec2 wheel{0.0f};  // +y scrolls down
  KeyModifiers modifiers = kModNone;
};

/**
 * @brief Everything that happened since the last frame
 *
 * Positions are in render-target pixels with the origin at the top left.
 */
struct UIInputFrame {
  glm::vec2 pointer{-1.0f};
  bool pointer_inside = true;  // false when the cursor left the window
  // False when something else (e.g. an ImGui dev window) wants the mouse
  // or keyboard this frame.
  bool mouse_available = true;
  bool keyboard_available = true;
  std::vector<UIInputEvent> events;
};

// Clipboard access, provided by the host (GLFW in games, ImGui in the editor).
struct UIClipboard {
  std::function<std::string()> get;
  std::function<void(const std::string&)> set;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_INPUT_H
