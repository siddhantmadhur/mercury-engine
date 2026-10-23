#include "input_script.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <format>
#include <sstream>

#include "debugger/debugger.h"
#include "input/input_actions.h"
#include "input/input_manager.h"
#include "os/filesystem.h"

namespace Mercury {

bool InputScript::Load(const std::string& file, std::string* error) {
  auto text = os::ReadText(file);
  if (!text) {
    if (error) *error = "could not open " + file;
    return false;
  }
  if (!LoadFromString(*text, error)) {
    if (error) *error = file + ":" + *error;
    return false;
  }
  return true;
}

bool InputScript::LoadFromString(const std::string& text, std::string* error) {
  steps_.clear();
  next_ = 0;
  std::istringstream lines(text);
  std::string line;
  int number = 0;
  while (std::getline(lines, line)) {
    number++;
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') continue;
    std::istringstream words(line);
    Step step;
    step.line = number;
    if (!(words >> step.frame >> step.command)) {
      if (error) *error = std::format("{}: expected <frame> <command>", number);
      return false;
    }
    std::getline(words, step.rest);
    step.rest.erase(0, step.rest.find_first_not_of(" \t"));
    while (!step.rest.empty() && (step.rest.back() == '\r' || step.rest.back() == ' ')) {
      step.rest.pop_back();
    }
    std::istringstream args(step.rest);
    for (std::string arg; args >> arg;) step.args.push_back(arg);
    static const char* kCommands[] = {"press", "release", "tap", "mouse", "look",
                                      "capture", "log", "quit"};
    if (std::find(std::begin(kCommands), std::end(kCommands), step.command) ==
        std::end(kCommands)) {
      if (error) *error = std::format("{}: unknown command {}", number, step.command);
      return false;
    }
    if ((step.command == "press" || step.command == "release" || step.command == "tap") &&
        (step.args.size() != 1 || KeyFromControl(step.args[0]) == GLFW_KEY_UNKNOWN)) {
      if (error) *error = std::format("{}: unknown key \"{}\"", number, step.rest);
      return false;
    }
    steps_.push_back(std::move(step));
  }
  std::stable_sort(steps_.begin(), steps_.end(),
                   [](const Step& a, const Step& b) { return a.frame < b.frame; });
  return true;
}

void InputScript::Apply(uint64_t frame, InputManager& input, GLFWwindow* window,
                        const Hooks& hooks) {
  for (int key : release_next_) input.HandleKeyCallback(window, key, 0, GLFW_RELEASE, 0);
  release_next_.clear();
  // Only scripted movement moves the view; the real mouse may be anywhere.
  input.ClearFrameCursorDelta();
  for (; next_ < steps_.size() && steps_[next_].frame <= frame; next_++) {
    const Step& step = steps_[next_];
    const auto& args = step.args;
    if (step.command == "press" || step.command == "tap") {
      const int key = KeyFromControl(args[0]);
      input.HandleKeyCallback(window, key, 0, GLFW_PRESS, 0);
      if (step.command == "tap") release_next_.push_back(key);
    } else if (step.command == "release") {
      input.HandleKeyCallback(window, KeyFromControl(args[0]), 0, GLFW_RELEASE, 0);
    } else if (step.command == "mouse" && args.size() == 2) {
      int button = GLFW_MOUSE_BUTTON_LEFT;
      if (args[0] == "right") button = GLFW_MOUSE_BUTTON_RIGHT;
      if (args[0] == "middle") button = GLFW_MOUSE_BUTTON_MIDDLE;
      input.HandleMouseButtonCallback(window, button,
                                      args[1] == "up" ? GLFW_RELEASE : GLFW_PRESS, 0);
    } else if (step.command == "look" && args.size() == 2) {
      input.AddFrameCursorDelta(
          glm::vec2(std::strtof(args[0].c_str(), nullptr), std::strtof(args[1].c_str(), nullptr)));
    } else if (step.command == "capture") {
      if (hooks.capture) hooks.capture(step.rest);
    } else if (step.command == "log") {
      PDebug::info("[input script {}] {}", frame, step.rest);
    } else if (step.command == "quit") {
      if (hooks.quit) hooks.quit();
    } else {
      PDebug::warn("Input script line {}: bad arguments for {}", step.line, step.command);
    }
  }
}

}  // namespace Mercury
