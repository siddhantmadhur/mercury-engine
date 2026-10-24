// mercury_editor_smoke <project.mproj> <script.txt>
//
// Runs the real editor and replays scripted input into ImGui, for smoke
// testing the UI without a person at the keyboard. Combine with
// MERCURY_CAPTURE / MERCURY_CAPTURE_FRAME to save a frame. Script lines:
//
//   <frame> move <x> <y>
//   <frame> click <x> <y>          (press now, release two frames later)
//   <frame> rclick <x> <y>         (right button, like click)
//   <frame> down | up              (left button)
//   <frame> drag <x> <y>           (move with the button held)
//   <frame> key <name>             (ctrl+s, ctrl+z, delete, enter, f, ...)
//   <frame> type <text...>
//   <frame> wheel <amount>
//   <frame> quit
//   # comments and blank lines are ignored

#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "application/default_application.h"
#include "debugger/debugger.h"
#include "editor_scene.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"

namespace {

struct Step {
  int frame;
  std::string command;
  std::vector<std::string> args;
  std::string rest;
};

ImGuiKey KeyFromName(const std::string& name) {
  static const std::map<std::string, ImGuiKey> keys = {
      {"delete", ImGuiKey_Delete}, {"enter", ImGuiKey_Enter},
      {"escape", ImGuiKey_Escape}, {"tab", ImGuiKey_Tab},
      {"f2", ImGuiKey_F2},         {"backspace", ImGuiKey_Backspace},
  };
  if (auto it = keys.find(name); it != keys.end()) return it->second;
  if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
    return static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'a'));
  }
  return ImGuiKey_None;
}

class ScriptedEditor : public Mercury::EditorScene {
 public:
  ScriptedEditor(std::filesystem::path project, std::vector<Step> steps)
      : EditorScene(std::move(project)), steps_(std::move(steps)) {}

  void OnFrame(double delta_t) override {
    // Events queued now are seen by ImGui on the next frame. Keys pressed
    // last frame are released first, so each press lasts one frame.
    ImGuiIO& io = ImGui::GetIO();
    for (ImGuiKey key : held_) io.AddKeyEvent(key, false);
    held_.clear();
    // While the real pointer is outside the window, the GLFW backend asks
    // GLFW for the cursor position every frame, which would undo the
    // scripted one. Telling it the cursor is inside turns that off.
    if (has_mouse_) {
      auto window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
      if (window) ImGui_ImplGlfw_CursorEnterCallback(window, GLFW_TRUE);
      io.AddMousePosEvent(mouse_.x, mouse_.y);
    }
    for (const auto& step : steps_) {
      if (step.frame != frame_) continue;
      if (step.command == "move" || step.command == "drag") {
        SetMouse(std::stof(step.args[0]), std::stof(step.args[1]));
      } else if (step.command == "click") {
        SetMouse(std::stof(step.args[0]), std::stof(step.args[1]));
        io.AddMouseButtonEvent(0, true);
        release_at_ = frame_ + 2;
      } else if (step.command == "rclick") {
        SetMouse(std::stof(step.args[0]), std::stof(step.args[1]));
        io.AddMouseButtonEvent(1, true);
        release_right_at_ = frame_ + 2;
      } else if (step.command == "down") {
        io.AddMouseButtonEvent(0, true);
      } else if (step.command == "up") {
        io.AddMouseButtonEvent(0, false);
      } else if (step.command == "wheel") {
        io.AddMouseWheelEvent(0.0f, std::stof(step.args[0]));
      } else if (step.command == "type") {
        io.AddInputCharactersUTF8(step.rest.c_str());
      } else if (step.command == "key") {
        PressKey(step.args[0]);
      } else if (step.command == "quit") {
        QuitScene();
      }
    }
    if (frame_ == release_at_) io.AddMouseButtonEvent(0, false);
    if (frame_ == release_right_at_) io.AddMouseButtonEvent(1, false);
    frame_++;
    EditorScene::OnFrame(delta_t);
  }

  // Skip the unsaved-changes prompt when the script quits.
  bool OnCloseRequested() override { return true; }

 private:
  void SetMouse(float x, float y) {
    mouse_ = ImVec2(x, y);
    has_mouse_ = true;
    ImGui::GetIO().AddMousePosEvent(x, y);
  }

  void PressKey(const std::string& chord) {
    ImGuiIO& io = ImGui::GetIO();
    std::stringstream parts(chord);
    std::string part;
    while (std::getline(parts, part, '+')) {
      ImGuiKey key = part == "ctrl"    ? ImGuiMod_Ctrl
                     : part == "shift" ? ImGuiMod_Shift
                                       : KeyFromName(part);
      if (key == ImGuiKey_None) {
        PDebug::error("smoke: unknown key {}", part);
        continue;
      }
      io.AddKeyEvent(key, true);
      held_.push_back(key);
    }
  }

  std::vector<Step> steps_;
  int frame_ = 0;
  int release_at_ = -1;
  int release_right_at_ = -1;
  std::vector<ImGuiKey> held_;
  ImVec2 mouse_ = {0, 0};
  bool has_mouse_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PDebug::error("usage: {} <project.mproj> <script.txt>", argv[0]);
    return 2;
  }
  std::vector<Step> steps;
  std::ifstream script(argv[2]);
  std::string line;
  while (std::getline(script, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream in(line);
    Step step;
    in >> step.frame >> step.command;
    std::getline(in >> std::ws, step.rest);
    std::stringstream words(step.rest);
    for (std::string word; words >> word;) step.args.push_back(word);
    steps.push_back(std::move(step));
  }

  auto application = std::make_unique<Mercury::DefaultApplication>(
      "Mercury Editor (smoke test)", 1600, 900);
  application->SetGameScene(
      std::make_unique<ScriptedEditor>(argv[1], std::move(steps)));
  return application->Run();
}
