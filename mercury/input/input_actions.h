/*
 * input_actions.h
 *
 * Unity's Input System actions: named actions (Jump, Move, Look) bound to
 * keys, mouse and gamepad controls, read by value or through callbacks.
 * The project lists its actions file in [input] actions = "res://...".
 *
 *   [minput format=1]
 *
 *   [action name="Player/Jump" type="button"]
 *   bindings = ["<Keyboard>/space", "<Gamepad>/buttonSouth"]
 *
 *   [action name="Player/Move" type="value" control="vector2"]
 *   bindings = ["<Gamepad>/leftStick"]
 *   composites = [["2DVector", ["<Keyboard>/w", "<Keyboard>/upArrow"],
 *                 ["<Keyboard>/s"], ["<Keyboard>/a"], ["<Keyboard>/d"]]]
 *
 * Control paths use Unity's names: <Keyboard>/w, <Keyboard>/leftShift,
 * <Mouse>/leftButton, <Mouse>/delta (also <Pointer>/delta), <Mouse>/scroll,
 * <Gamepad>/buttonSouth, <Gamepad>/leftStick, <Gamepad>/rightTrigger...
 *
 *   auto* jump = GetInputActions()->Find("Player/Jump");
 *   jump->performed += [this](const InputAction::CallbackContext&) { Jump(); };
 *   glm::vec2 move = actions->Find("Player/Move")->ReadValue<glm::vec2>();
 */
#ifndef MERCURY_INPUT_INPUT_ACTIONS_H
#define MERCURY_INPUT_INPUT_ACTIONS_H

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "glm/vec2.hpp"

namespace Mercury {

class InputManager;

class InputAction {
 public:
  enum class Type { kButton, kValue, kPassThrough };
  enum class Phase { kDisabled, kWaiting, kStarted, kPerformed };

  struct CallbackContext {
    const InputAction* action = nullptr;
    // Name of the control that caused it, e.g. "space" or "1"
    // (Unity's ctx.control.name).
    std::string control;
    glm::vec2 value = glm::vec2(0.0f);
    Phase phase = Phase::kWaiting;

    template <typename T>
    T ReadValue() const;
  };

  using Callback = std::function<void(const CallbackContext&)>;

  // A list of callbacks: `action.performed += fn;` returns an id for -=.
  class Signal {
   public:
    int operator+=(Callback callback) {
      callbacks_.push_back({next_id_, std::move(callback)});
      return next_id_++;
    }
    void operator-=(int id) {
      std::erase_if(callbacks_, [id](const auto& c) { return c.first == id; });
    }
    void Clear() { callbacks_.clear(); }
    void Invoke(const CallbackContext& context) const {
      // Copy: a callback may add or remove callbacks.
      const auto callbacks = callbacks_;
      for (const auto& [id, fn] : callbacks) fn(context);
    }

   private:
    std::vector<std::pair<int, Callback>> callbacks_;
    int next_id_ = 1;
  };

  InputAction(std::string map, std::string name, Type type, bool vector2);

  const std::string& GetName() const { return name_; }
  const std::string& GetMapName() const { return map_; }
  Type GetType() const { return type_; }
  Phase GetPhase() const { return phase_; }

  // The current value: float (magnitude, or x), glm::vec2, or bool.
  template <typename T>
  T ReadValue() const;

  bool IsPressed() const { return pressed_; }
  bool WasPressedThisFrame() const { return pressed_this_frame_; }
  bool WasReleasedThisFrame() const { return released_this_frame_; }

  void Enable();
  void Disable();
  bool IsEnabled() const { return enabled_; }

  Signal started;
  Signal performed;
  Signal canceled;

  // Bindings: single controls, or 2D composites of up/down/left/right parts
  // (each part may list several controls).
  void AddBinding(std::string path) { bindings_.push_back(std::move(path)); }
  void AddComposite2D(std::vector<std::string> up, std::vector<std::string> down,
                      std::vector<std::string> left, std::vector<std::string> right);

  // Reads the controls and fires callbacks; called once per frame.
  void Update(const InputManager& input);

 private:
  struct Composite {
    std::vector<std::string> up, down, left, right;
  };

  // The bound control with the largest actuation this frame.
  glm::vec2 Evaluate(const InputManager& input, std::string* control) const;
  void Fire(const Signal& signal, Phase phase);

  std::string map_;
  std::string name_;
  Type type_;
  bool vector2_ = false;
  std::vector<std::string> bindings_;
  std::vector<Composite> composites_;

  bool enabled_ = true;
  Phase phase_ = Phase::kWaiting;
  glm::vec2 value_ = glm::vec2(0.0f);
  std::string control_;
  bool pressed_ = false;
  bool pressed_this_frame_ = false;
  bool released_this_frame_ = false;
};

class InputActionAsset {
 public:
  bool Load(const std::filesystem::path& file, std::string* error = nullptr);
  bool LoadFromString(std::string_view source, std::string* error = nullptr);

  // "Map/Action", or a bare action name (first match).
  InputAction* Find(std::string_view name);
  std::vector<InputAction*> GetMap(std::string_view map);

  // Enable or disable every action of a map ("" for all).
  void EnableMap(std::string_view map = "");
  void DisableMap(std::string_view map = "");

  void Update(const InputManager& input);

  const std::vector<std::unique_ptr<InputAction>>& GetActions() const { return actions_; }

 private:
  std::vector<std::unique_ptr<InputAction>> actions_;
};

// GLFW key for a Unity keyboard control name ("w", "leftShift", "f5");
// GLFW_KEY_UNKNOWN if there is none.
int KeyFromControl(std::string_view control);

/**
 * @brief Current value of one control path such as "<Keyboard>/space" or
 * "<Gamepad>/leftStick" (x, y); buttons read 0 or 1 in x
 */
glm::vec2 ReadControl(const InputManager& input, std::string_view path);

template <>
float InputAction::ReadValue<float>() const;
template <>
glm::vec2 InputAction::ReadValue<glm::vec2>() const;
template <>
bool InputAction::ReadValue<bool>() const;

template <typename T>
T InputAction::CallbackContext::ReadValue() const {
  if constexpr (std::is_same_v<T, glm::vec2>) {
    return value;
  } else if constexpr (std::is_same_v<T, bool>) {
    return value.x != 0.0f || value.y != 0.0f;
  } else {
    return static_cast<T>(value.x);
  }
}

}  // namespace Mercury

#endif  // MERCURY_INPUT_INPUT_ACTIONS_H
