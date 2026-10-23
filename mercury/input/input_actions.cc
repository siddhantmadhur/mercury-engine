#include "input_actions.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "GLFW/glfw3.h"
#include "glm/geometric.hpp"
#include "input/input_manager.h"
#include "os/filesystem.h"
#include "serialization/mtext.h"

namespace Mercury {

namespace {

constexpr float kPressPoint = 0.5f;
constexpr float kStickDeadzone = 0.125f;

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

float Deadzone(float value) {
  const float magnitude = std::abs(value);
  if (magnitude < kStickDeadzone) return 0.0f;
  return std::copysign((magnitude - kStickDeadzone) / (1.0f - kStickDeadzone), value);
}

glm::vec2 Stick(const GamepadState& pad, int x_axis, int y_axis) {
  // GLFW's Y points down; Unity's stick Y points up.
  glm::vec2 v(pad.axes[x_axis], -pad.axes[y_axis]);
  const float length = glm::length(v);
  if (length < kStickDeadzone) return glm::vec2(0.0f);
  const float scaled = std::min((length - kStickDeadzone) / (1.0f - kStickDeadzone), 1.0f);
  return v / length * scaled;
}

glm::vec2 ReadGamepad(const GamepadState& pad, std::string_view control) {
  if (!pad.connected) return glm::vec2(0.0f);
  const std::string name = Lower(control);
  auto button = [&](int b) { return glm::vec2(pad.buttons[b] ? 1.0f : 0.0f, 0.0f); };
  static const std::map<std::string, int> kButtons = {
      {"buttonsouth", GLFW_GAMEPAD_BUTTON_A},
      {"buttoneast", GLFW_GAMEPAD_BUTTON_B},
      {"buttonwest", GLFW_GAMEPAD_BUTTON_X},
      {"buttonnorth", GLFW_GAMEPAD_BUTTON_Y},
      {"leftshoulder", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
      {"rightshoulder", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
      {"select", GLFW_GAMEPAD_BUTTON_BACK},
      {"start", GLFW_GAMEPAD_BUTTON_START},
      {"leftstickpress", GLFW_GAMEPAD_BUTTON_LEFT_THUMB},
      {"rightstickpress", GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
      {"dpad/up", GLFW_GAMEPAD_BUTTON_DPAD_UP},
      {"dpad/down", GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
      {"dpad/left", GLFW_GAMEPAD_BUTTON_DPAD_LEFT},
      {"dpad/right", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
  };
  if (auto it = kButtons.find(name); it != kButtons.end()) return button(it->second);
  if (name == "lefttrigger") return {pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER], 0.0f};
  if (name == "righttrigger") return {pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER], 0.0f};
  if (name == "leftstick") return Stick(pad, GLFW_GAMEPAD_AXIS_LEFT_X, GLFW_GAMEPAD_AXIS_LEFT_Y);
  if (name == "rightstick") {
    return Stick(pad, GLFW_GAMEPAD_AXIS_RIGHT_X, GLFW_GAMEPAD_AXIS_RIGHT_Y);
  }
  if (name == "leftstick/x") return {Deadzone(pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X]), 0.0f};
  if (name == "leftstick/y") return {-Deadzone(pad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]), 0.0f};
  if (name == "rightstick/x") return {Deadzone(pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]), 0.0f};
  if (name == "rightstick/y") return {-Deadzone(pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]), 0.0f};
  if (name == "dpad") {
    return {static_cast<float>(pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) -
                static_cast<float>(pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]),
            static_cast<float>(pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]) -
                static_cast<float>(pad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN])};
  }
  return glm::vec2(0.0f);
}

std::string_view ControlName(std::string_view path) {
  const auto slash = path.find('/');
  if (slash == std::string_view::npos) return path;
  return path.substr(slash + 1);
}

}  // namespace

int KeyFromControl(std::string_view control) {
  const std::string name = Lower(control);
  if (name.size() == 1) {
    const char c = name[0];
    if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
    if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
  }
  if (name.size() >= 2 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1]))) {
    const int n = std::atoi(name.c_str() + 1);
    if (n >= 1 && n <= 25) return GLFW_KEY_F1 + n - 1;
  }
  if (name.starts_with("numpad") && name.size() == 7 &&
      std::isdigit(static_cast<unsigned char>(name[6]))) {
    return GLFW_KEY_KP_0 + (name[6] - '0');
  }
  static const std::map<std::string, int> kKeys = {
      {"space", GLFW_KEY_SPACE},
      {"enter", GLFW_KEY_ENTER},
      {"numpadenter", GLFW_KEY_KP_ENTER},
      {"escape", GLFW_KEY_ESCAPE},
      {"tab", GLFW_KEY_TAB},
      {"backspace", GLFW_KEY_BACKSPACE},
      {"backquote", GLFW_KEY_GRAVE_ACCENT},
      {"quote", GLFW_KEY_APOSTROPHE},
      {"semicolon", GLFW_KEY_SEMICOLON},
      {"comma", GLFW_KEY_COMMA},
      {"period", GLFW_KEY_PERIOD},
      {"slash", GLFW_KEY_SLASH},
      {"backslash", GLFW_KEY_BACKSLASH},
      {"leftbracket", GLFW_KEY_LEFT_BRACKET},
      {"rightbracket", GLFW_KEY_RIGHT_BRACKET},
      {"minus", GLFW_KEY_MINUS},
      {"equals", GLFW_KEY_EQUAL},
      {"uparrow", GLFW_KEY_UP},
      {"downarrow", GLFW_KEY_DOWN},
      {"leftarrow", GLFW_KEY_LEFT},
      {"rightarrow", GLFW_KEY_RIGHT},
      {"leftshift", GLFW_KEY_LEFT_SHIFT},
      {"rightshift", GLFW_KEY_RIGHT_SHIFT},
      {"leftctrl", GLFW_KEY_LEFT_CONTROL},
      {"rightctrl", GLFW_KEY_RIGHT_CONTROL},
      {"leftalt", GLFW_KEY_LEFT_ALT},
      {"rightalt", GLFW_KEY_RIGHT_ALT},
      {"leftmeta", GLFW_KEY_LEFT_SUPER},
      {"rightmeta", GLFW_KEY_RIGHT_SUPER},
      {"capslock", GLFW_KEY_CAPS_LOCK},
      {"delete", GLFW_KEY_DELETE},
      {"insert", GLFW_KEY_INSERT},
      {"home", GLFW_KEY_HOME},
      {"end", GLFW_KEY_END},
      {"pageup", GLFW_KEY_PAGE_UP},
      {"pagedown", GLFW_KEY_PAGE_DOWN},
  };
  auto it = kKeys.find(name);
  return it == kKeys.end() ? GLFW_KEY_UNKNOWN : it->second;
}

glm::vec2 ReadControl(const InputManager& const_input, std::string_view path) {
  auto& input = const_cast<InputManager&>(const_input);
  const auto close = path.find('>');
  if (path.empty() || path[0] != '<' || close == std::string_view::npos) {
    return glm::vec2(0.0f);
  }
  const std::string device = Lower(path.substr(1, close - 1));
  std::string_view control = path.substr(close + 1);
  if (!control.empty() && control[0] == '/') control.remove_prefix(1);

  if (device == "keyboard") {
    // Text input in dev windows should not move the player.
    if (input.ImGuiWantsKeyboard()) return glm::vec2(0.0f);
    const std::string name = Lower(control);
    if (name == "anykey") {
      for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++) {
        if (input.IsPressed(key)) return {1.0f, 0.0f};
      }
      return glm::vec2(0.0f);
    }
    if (name == "shift") {
      return {input.IsPressed(GLFW_KEY_LEFT_SHIFT) || input.IsPressed(GLFW_KEY_RIGHT_SHIFT)
                  ? 1.0f : 0.0f, 0.0f};
    }
    if (name == "ctrl") {
      return {input.IsPressed(GLFW_KEY_LEFT_CONTROL) ||
                      input.IsPressed(GLFW_KEY_RIGHT_CONTROL) ? 1.0f : 0.0f,
              0.0f};
    }
    const int key = KeyFromControl(control);
    if (key == GLFW_KEY_UNKNOWN) return glm::vec2(0.0f);
    return {input.IsPressed(key) ? 1.0f : 0.0f, 0.0f};
  }
  if (device == "mouse" || device == "pointer") {
    const std::string name = Lower(control);
    if (name == "delta") {
      // Window Y grows down; Unity's mouse delta Y grows up.
      const glm::vec2 delta = input.GetFrameCursorDelta();
      return {delta.x, -delta.y};
    }
    if (name == "delta/x") return {input.GetFrameCursorDelta().x, 0.0f};
    if (name == "delta/y") return {-input.GetFrameCursorDelta().y, 0.0f};
    if (name == "scroll") return input.GetFrameScroll();
    if (name == "scroll/y") return {input.GetFrameScroll().y, 0.0f};
    if (name == "position") return input.GetCursorPos();
    if (input.ImGuiWantsMouse()) return glm::vec2(0.0f);
    if (name == "leftbutton" || name == "press") {
      return {input.IsPressed(MouseButton::kLeft) ? 1.0f : 0.0f, 0.0f};
    }
    if (name == "rightbutton") return {input.IsPressed(MouseButton::kRight) ? 1.0f : 0.0f, 0.0f};
    if (name == "middlebutton") {
      return {input.IsPressed(MouseButton::kMiddle) ? 1.0f : 0.0f, 0.0f};
    }
    return glm::vec2(0.0f);
  }
  if (device == "gamepad" || device == "xinputcontroller" || device == "dualshockgamepad") {
    return ReadGamepad(input.GetGamepad(), control);
  }
  // Touchscreen, XR and joystick bindings are accepted but never actuate.
  return glm::vec2(0.0f);
}

// ------------------------------------------------------------- InputAction --

InputAction::InputAction(std::string map, std::string name, Type type, bool vector2)
    : map_(std::move(map)), name_(std::move(name)), type_(type), vector2_(vector2) {}

void InputAction::AddComposite2D(std::vector<std::string> up, std::vector<std::string> down,
                                 std::vector<std::string> left,
                                 std::vector<std::string> right) {
  composites_.push_back({std::move(up), std::move(down), std::move(left), std::move(right)});
}

glm::vec2 InputAction::Evaluate(const InputManager& input, std::string* control) const {
  glm::vec2 best(0.0f);
  float best_magnitude = 0.0f;
  auto consider = [&](glm::vec2 value, std::string_view path) {
    const float magnitude = glm::length(value);
    if (magnitude > best_magnitude) {
      best = value;
      best_magnitude = magnitude;
      if (control) *control = std::string(ControlName(path));
    }
  };
  for (const auto& path : bindings_) consider(ReadControl(input, path), path);
  for (const auto& composite : composites_) {
    auto part = [&](const std::vector<std::string>& paths, std::string* which) {
      float value = 0.0f;
      for (const auto& path : paths) {
        const float v = ReadControl(input, path).x;
        if (v > value) {
          value = v;
          *which = path;
        }
      }
      return value;
    };
    std::string up, down, left, right;
    glm::vec2 v(part(composite.right, &right) - part(composite.left, &left),
                part(composite.up, &up) - part(composite.down, &down));
    // Unity's default 2D vector mode, DigitalNormalized.
    if (glm::length(v) > 1.0f) v = glm::normalize(v);
    const std::string& which = !up.empty() ? up : !down.empty() ? down : !left.empty() ? left : right;
    consider(v, which);
  }
  return best;
}

void InputAction::Fire(const Signal& signal, Phase phase) {
  CallbackContext context;
  context.action = this;
  context.control = control_;
  context.value = value_;
  context.phase = phase;
  signal.Invoke(context);
}

void InputAction::Update(const InputManager& input) {
  pressed_this_frame_ = false;
  released_this_frame_ = false;
  if (!enabled_) return;

  std::string control;
  const glm::vec2 value = Evaluate(input, &control);
  const glm::vec2 previous = value_;
  value_ = vector2_ ? value : glm::vec2(glm::length(value) > 0 ? value.x : 0.0f, 0.0f);
  if (!control.empty()) control_ = control;

  const bool was_pressed = pressed_;
  pressed_ = glm::length(value) >= kPressPoint;
  pressed_this_frame_ = pressed_ && !was_pressed;
  released_this_frame_ = !pressed_ && was_pressed;

  switch (type_) {
    case Type::kButton:
      if (pressed_this_frame_) {
        phase_ = Phase::kStarted;
        Fire(started, Phase::kStarted);
        phase_ = Phase::kPerformed;
        Fire(performed, Phase::kPerformed);
      } else if (released_this_frame_) {
        phase_ = Phase::kWaiting;
        Fire(canceled, Phase::kWaiting);
      }
      break;
    case Type::kValue: {
      const bool actuated = glm::length(value) > 0.0f;
      if (actuated && phase_ == Phase::kWaiting) {
        phase_ = Phase::kStarted;
        Fire(started, Phase::kStarted);
        phase_ = Phase::kPerformed;
        Fire(performed, Phase::kPerformed);
      } else if (actuated && value != previous) {
        Fire(performed, Phase::kPerformed);
      } else if (!actuated && phase_ != Phase::kWaiting) {
        phase_ = Phase::kWaiting;
        Fire(canceled, Phase::kWaiting);
      }
      break;
    }
    case Type::kPassThrough:
      if (value != previous) {
        phase_ = Phase::kPerformed;
        Fire(performed, Phase::kPerformed);
      }
      break;
  }
}

void InputAction::Enable() {
  if (enabled_) return;
  enabled_ = true;
  phase_ = Phase::kWaiting;
}

void InputAction::Disable() {
  if (!enabled_) return;
  if (phase_ == Phase::kStarted || phase_ == Phase::kPerformed) {
    value_ = glm::vec2(0.0f);
    Fire(canceled, Phase::kWaiting);
  }
  enabled_ = false;
  phase_ = Phase::kDisabled;
  value_ = glm::vec2(0.0f);
  pressed_ = false;
}

template <>
float InputAction::ReadValue<float>() const {
  return enabled_ ? value_.x : 0.0f;
}

template <>
glm::vec2 InputAction::ReadValue<glm::vec2>() const {
  return enabled_ ? value_ : glm::vec2(0.0f);
}

template <>
bool InputAction::ReadValue<bool>() const {
  return enabled_ && pressed_;
}

// -------------------------------------------------------- InputActionAsset --

bool InputActionAsset::Load(const std::filesystem::path& file, std::string* error) {
  auto text = os::ReadText(file);
  if (!text) {
    if (error) *error = "could not open " + file.string();
    return false;
  }
  if (!LoadFromString(*text, error)) {
    if (error) *error = file.string() + ": " + *error;
    return false;
  }
  return true;
}

bool InputActionAsset::LoadFromString(std::string_view source, std::string* error) {
  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::Parse(source, document, &parse_error)) {
    if (error) *error = parse_error.ToString();
    return false;
  }
  actions_.clear();
  auto strings = [](const mtext::Variant* value) {
    std::vector<std::string> out;
    if (!value) return out;
    if (auto text = value->ToString()) {
      out.push_back(*text);
    } else if (auto array = value->Get<mtext::Array>()) {
      for (const auto& item : *array) {
        if (auto text = item.ToString()) out.push_back(*text);
      }
    }
    return out;
  };
  for (const auto& section : document.sections) {
    if (section.tag != "action") continue;
    const std::string full = section.attributes.GetString("name");
    const auto slash = full.find('/');
    const std::string map = slash == std::string::npos ? "" : full.substr(0, slash);
    const std::string name = slash == std::string::npos ? full : full.substr(slash + 1);
    const std::string type_name = Lower(section.attributes.GetString("type", "button"));
    InputAction::Type type = InputAction::Type::kButton;
    if (type_name == "value") type = InputAction::Type::kValue;
    if (type_name == "passthrough") type = InputAction::Type::kPassThrough;
    const bool vector2 = Lower(section.attributes.GetString("control")) == "vector2";
    auto action = std::make_unique<InputAction>(map, name, type, vector2);
    for (const auto& path : strings(section.properties.Get("bindings"))) {
      action->AddBinding(path);
    }
    if (auto composites = section.properties.Get("composites")) {
      if (auto list = composites->Get<mtext::Array>()) {
        for (const auto& item : *list) {
          auto parts = item.Get<mtext::Array>();
          if (!parts || parts->size() < 5) continue;
          action->AddComposite2D(strings(&(*parts)[1]), strings(&(*parts)[2]),
                                 strings(&(*parts)[3]), strings(&(*parts)[4]));
        }
      }
    }
    actions_.push_back(std::move(action));
  }
  return true;
}

InputAction* InputActionAsset::Find(std::string_view name) {
  const auto slash = name.find('/');
  for (auto& action : actions_) {
    if (slash == std::string_view::npos) {
      if (action->GetName() == name) return action.get();
    } else if (action->GetMapName() == name.substr(0, slash) &&
               action->GetName() == name.substr(slash + 1)) {
      return action.get();
    }
  }
  return nullptr;
}

std::vector<InputAction*> InputActionAsset::GetMap(std::string_view map) {
  std::vector<InputAction*> result;
  for (auto& action : actions_) {
    if (map.empty() || action->GetMapName() == map) result.push_back(action.get());
  }
  return result;
}

void InputActionAsset::EnableMap(std::string_view map) {
  for (InputAction* action : GetMap(map)) action->Enable();
}

void InputActionAsset::DisableMap(std::string_view map) {
  for (InputAction* action : GetMap(map)) action->Disable();
}

void InputActionAsset::Update(const InputManager& input) {
  for (auto& action : actions_) action->Update(input);
}

}  // namespace Mercury
