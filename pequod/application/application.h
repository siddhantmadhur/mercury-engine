/*
 * application.h
 *
 * Refactoring of the engine where I've made a decision to make my own graphics
 * api wrapper. It will be simple and only targets Windows. This decision was
 * made so I can hyper-focus on one platform which means it will be easier to
 * debug any bugs
 *
 */

#ifndef PEQUODENGINE_APPLICATION_H
#define PEQUODENGINE_APPLICATION_H
#include <input/input_manager.h>

#include <string>
#include <string_view>

#include "GLFW/glfw3.h"
#include "scene/scene.h"

namespace Pequod {
static InputManager input_manager_;
class Application {
 public:
  Application(const std::string& window_title);
  virtual ~Application();
  int Run();
  void Quit() const;
  void SetGameScene(std::unique_ptr<GameScene>);

 protected:
  virtual bool OnLoad() = 0;  // Runs when the application is created
  virtual void Render() = 0;  // Renders objects
  virtual bool Initialize();
  virtual void OnResize(int32_t width, int32_t height);
  static void HandleResize(GLFWwindow* window, int32_t width, int32_t height);
  static void HandleKeyCallback(GLFWwindow* window, int key, int scancode,
                                int action, int mods);

  int32_t GetWidth() const;
  int32_t GetHeight() const;
  GLFWwindow* GetWindow() const;

 protected:
  std::unique_ptr<GameScene> game_scene_ = nullptr;

 private:
  GLFWwindow* window_ = nullptr;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::string title_ = "Pequod";
  bool is_loaded_ = false;

  double delta_time_ = 0.0f;
  double time_elapsed_ = 0.0f;
  int64_t last_tick_ = 0;
  double time_since_last_tick_ = 0.0f;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_APPLICATION_H
