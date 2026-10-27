#include "input_actions.h"

#include <algorithm>
#include <cctype>

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

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
    return std::tolower(x) == std::tolower(y);
  });
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

// Unity's gamepad button names, GLFW button index.
constexpr std::pair<std::string_view, int> kGamepadButtons[] = {
    {"buttonSouth", GLFW_GAMEPAD_BUTTON_A},
    {"buttonEast", GLFW_GAMEPAD_BUTTON_B},
    {"buttonWest", GLFW_GAMEPAD_BUTTON_X},
    {"buttonNorth", GLFW_GAMEPAD_BUTTON_Y},
    {"leftShoulder", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
    {"rightShoulder", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
    {"select", GLFW_GAMEPAD_BUTTON_BACK},
    {"start", GLFW_GAMEPAD_BUTTON_START},
    {"leftStickPress", GLFW_GAMEPAD_BUTTON_LEFT_THUMB},
    {"rightStickPress", GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
    {"dpad/up", GLFW_GAMEPAD_BUTTON_DPAD_UP},
    {"dpad/down", GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
    {"dpad/left", GLFW_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpad/right", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
};

// Unity's names for the keys that are not a letter, digit, F-key or numpad
// digit (those are generated in KeyFromControl / ControlFromKey).
constexpr std::pair<std::string_view, int> kNamedKeys[] = {
    {"space", GLFW_KEY_SPACE},
    {"enter", GLFW_KEY_ENTER},
    {"numpadEnter", GLFW_KEY_KP_ENTER},
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
    {"leftBracket", GLFW_KEY_LEFT_BRACKET},
    {"rightBracket", GLFW_KEY_RIGHT_BRACKET},
    {"minus", GLFW_KEY_MINUS},
    {"equals", GLFW_KEY_EQUAL},
    {"upArrow", GLFW_KEY_UP},
    {"downArrow", GLFW_KEY_DOWN},
    {"leftArrow", GLFW_KEY_LEFT},
    {"rightArrow", GLFW_KEY_RIGHT},
    {"leftShift", GLFW_KEY_LEFT_SHIFT},
    {"rightShift", GLFW_KEY_RIGHT_SHIFT},
    {"leftCtrl", GLFW_KEY_LEFT_CONTROL},
    {"rightCtrl", GLFW_KEY_RIGHT_CONTROL},
    {"leftAlt", GLFW_KEY_LEFT_ALT},
    {"rightAlt", GLFW_KEY_RIGHT_ALT},
    {"leftMeta", GLFW_KEY_LEFT_SUPER},
    {"rightMeta", GLFW_KEY_RIGHT_SUPER},
    {"capsLock", GLFW_KEY_CAPS_LOCK},
    {"delete", GLFW_KEY_DELETE},
    {"insert", GLFW_KEY_INSERT},
    {"home", GLFW_KEY_HOME},
    {"end", GLFW_KEY_END},
    {"pageUp", GLFW_KEY_PAGE_UP},
    {"pageDown", GLFW_KEY_PAGE_DOWN},
};

constexpr std::string_view kMouseControls[] = {
    "leftButton", "rightButton", "middleButton", "delta", "delta/x",
    "delta/y",    "scroll",      "scroll/y",     "position",
};

constexpr std::string_view kGamepadControls[] = {
    "leftStick",   "rightStick",   "leftStick/x",  "leftStick/y", "rightStick/x",
    "rightStick/y", "leftTrigger", "rightTrigger", "dpad",
};

glm::vec2 ReadGamepad(const GamepadState& pad, std::string_view control) {
  if (!pad.connected) return glm::vec2(0.0f);
  const std::string name = Lower(control);
  for (const auto& [button, index] : kGamepadButtons) {
    if (EqualsIgnoreCase(button, name)) return glm::vec2(pad.buttons[index] ? 1.0f : 0.0f, 0.0f);
  }
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
  for (const auto& [key_name, key] : kNamedKeys) {
    if (EqualsIgnoreCase(key_name, name)) return key;
  }
  return GLFW_KEY_UNKNOWN;
}

std::string ControlFromKey(int key) {
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return std::string(1, 'a' + (key - GLFW_KEY_A));
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return std::string(1, '0' + (key - GLFW_KEY_0));
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "f" + std::to_string(key - GLFW_KEY_F1 + 1);
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
    return "numpad" + std::to_string(key - GLFW_KEY_KP_0);
  }
  for (const auto& [name, named_key] : kNamedKeys) {
    if (named_key == key) return std::string(name);
  }
  return "";
}

std::string ControlFromGamepadButton(int button) {
  for (const auto& [name, index] : kGamepadButtons) {
    if (index == button) return std::string(name);
  }
  return "";
}

std::span<const std::string_view> KnownControlPaths() {
  static const std::vector<std::string> storage = [] {
    std::vector<std::string> paths;
    auto keyboard = [&](std::string_view name) {
      paths.push_back("<Keyboard>/" + std::string(name));
    };
    for (char c = 'a'; c <= 'z'; c++) keyboard(std::string(1, c));
    for (char c = '0'; c <= '9'; c++) keyboard(std::string(1, c));
    for (const auto& [name, key] : kNamedKeys) keyboard(name);
    keyboard("shift");
    keyboard("ctrl");
    keyboard("anyKey");
    for (int n = 1; n <= 12; n++) keyboard("f" + std::to_string(n));
    for (int n = 0; n <= 9; n++) keyboard("numpad" + std::to_string(n));
    for (auto name : kMouseControls) paths.push_back("<Mouse>/" + std::string(name));
    for (const auto& [name, button] : kGamepadButtons) {
      paths.push_back("<Gamepad>/" + std::string(name));
    }
    for (auto name : kGamepadControls) paths.push_back("<Gamepad>/" + std::string(name));
    return paths;
  }();
  static const std::vector<std::string_view> views(storage.begin(), storage.end());
  return views;
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
  InputActionsDesc desc;
  if (!ParseInputActions(source, &desc, error)) return false;
  actions_.clear();
  for (const auto& action_desc : desc.actions) {
    auto action = std::make_unique<InputAction>(action_desc.map, action_desc.name,
                                                action_desc.type, action_desc.vector2);
    for (const auto& path : action_desc.bindings) action->AddBinding(path);
    for (const auto& c : action_desc.composites) {
      action->AddComposite2D(c.up, c.down, c.left, c.right);
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

// --------------------------------------------------------- InputActionsDesc --

namespace {

const char* TypeToString(InputAction::Type type) {
  switch (type) {
    case InputAction::Type::kValue:
      return "value";
    case InputAction::Type::kPassThrough:
      return "passthrough";
    case InputAction::Type::kButton:
      break;
  }
  return "button";
}

mtext::Variant StringArray(const std::vector<std::string>& strings) {
  mtext::Array array;
  for (const auto& s : strings) array.emplace_back(s);
  return array;
}

mtext::Document ToDocument(const InputActionsDesc& desc) {
  mtext::Document document;
  auto& header = document.Add("minput").attributes;
  header.Set("format", 1);
  if (!desc.class_name.empty()) header.Set("class", desc.class_name);
  if (!desc.header.empty()) header.Set("header", desc.header);
  for (const auto& map : desc.maps) document.Add("map").attributes.Set("name", map);
  for (const auto& action : desc.actions) {
    auto& section = document.Add("action");
    section.attributes.Set("name", action.FullName());
    section.attributes.Set("type", TypeToString(action.type));
    if (action.vector2) section.attributes.Set("control", "vector2");
    if (!action.bindings.empty()) section.properties.Set("bindings", StringArray(action.bindings));
    if (!action.composites.empty()) {
      mtext::Array composites;
      for (const auto& c : action.composites) {
        composites.emplace_back(mtext::Array{"2DVector", StringArray(c.up), StringArray(c.down),
                                             StringArray(c.left), StringArray(c.right)});
      }
      section.properties.Set("composites", std::move(composites));
    }
  }
  return document;
}

}  // namespace

bool ParseInputActions(std::string_view source, InputActionsDesc* out, std::string* error) {
  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::Parse(source, document, &parse_error)) {
    if (error) *error = parse_error.ToString();
    return false;
  }
  *out = {};
  auto add_map = [&](const std::string& map) {
    if (std::find(out->maps.begin(), out->maps.end(), map) == out->maps.end()) {
      out->maps.push_back(map);
    }
  };
  auto strings = [](const mtext::Variant* value) {
    std::vector<std::string> result;
    if (!value) return result;
    if (auto text = value->ToString()) {
      result.push_back(*text);
    } else if (auto array = value->Get<mtext::Array>()) {
      for (const auto& item : *array) {
        if (auto text = item.ToString()) result.push_back(*text);
      }
    }
    return result;
  };
  for (const auto& section : document.sections) {
    if (section.tag == "minput") {
      out->header = section.attributes.GetString("header");
      out->class_name = section.attributes.GetString("class");
      continue;
    }
    if (section.tag == "map") {
      add_map(section.attributes.GetString("name"));
      continue;
    }
    if (section.tag != "action") continue;
    InputActionDesc action;
    const std::string full = section.attributes.GetString("name");
    const auto slash = full.find('/');
    action.map = slash == std::string::npos ? "" : full.substr(0, slash);
    action.name = slash == std::string::npos ? full : full.substr(slash + 1);
    const std::string type_name = Lower(section.attributes.GetString("type", "button"));
    if (type_name == "value") action.type = InputAction::Type::kValue;
    if (type_name == "passthrough") action.type = InputAction::Type::kPassThrough;
    action.vector2 = Lower(section.attributes.GetString("control")) == "vector2";
    action.bindings = strings(section.properties.Get("bindings"));
    if (auto composites = section.properties.Get("composites")) {
      if (auto list = composites->Get<mtext::Array>()) {
        for (const auto& item : *list) {
          auto parts = item.Get<mtext::Array>();
          if (!parts || parts->size() < 5) continue;
          action.composites.push_back({strings(&(*parts)[1]), strings(&(*parts)[2]),
                                       strings(&(*parts)[3]), strings(&(*parts)[4])});
        }
      }
    }
    add_map(action.map);
    out->actions.push_back(std::move(action));
  }
  return true;
}

std::string SerializeInputActions(const InputActionsDesc& desc) {
  return mtext::Write(ToDocument(desc));
}

bool SaveInputActions(const std::filesystem::path& file, const InputActionsDesc& desc) {
  return mtext::SaveFile(file, ToDocument(desc));
}

}  // namespace Mercury
