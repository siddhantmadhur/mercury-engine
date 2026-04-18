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
#include <string>
#include <string_view>

#include "GLFW/glfw3.h"

namespace Pequod {
class Application {
 public:
  Application(const std::string& window_title);
  ~Application();
  int Run();

 protected:
  virtual bool OnLoad() = 0;  // Runs when the application is created
  virtual void
  UpdateOnTick() = 0;  // Runs every tick (takes priority over frame)
  virtual void UpdateOnFrame() = 0;  // Runs every frame
  virtual void Render() = 0;         // Renders objects
  virtual bool Initialize();
  virtual void OnResize(int32_t width, int32_t height);
  static void HandleResize(GLFWwindow* window, int32_t width, int32_t height);

  const int32_t GetWidth();
  const int32_t GetHeight();
  GLFWwindow* GetWindow();

 private:
  GLFWwindow* window_ = nullptr;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::string_view title_ = "Pequod";
  bool is_loaded_ = false;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_APPLICATION_H
