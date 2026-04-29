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

/**
 * @brief Create a new cross-platform application using GLFW
 *
 * Abstraction to create cross-platform windowing and event handling. In order
 * to actually support every platform this class needs to be extended with the
 * renderer of choice (eg. d3d11, vulkan)
 *
 * This is easier to do because of the engine relying on a simple Primitive
 * data structure that defines the types of objects needed to be rendered along
 * with the types of shaders required for each backend.
 *
 */
class Application {
 public:
  Application(const std::string& window_title, float initial_width = 1280,
              float initial_height = 720);
  virtual ~Application();
  /**
   * Initializes and runs the program only ending once Quit() is ran or window
   * is closed
   * @return status code
   */
  int Run();

  /**
   * @brief Quit application window
   */
  void Quit() const;

  /**
   * Changes the scene context being displayed. NOT thread-safe!
   */
  void SetGameScene(std::unique_ptr<GameScene>);

 protected:
  /**
   * @brief Runs when the application context is created
   * @return status
   *
   * When implementing a backend this needs to be overloaded with the device
   * context setup. Things like creating shaders, creating resources need to be
   * done here. Make sure to first run the original Initialize function first
   */
  virtual bool Initialize();

  /**
   * @brief Runs after the Application is initialized
   * @return status
   */
  virtual bool OnLoad() = 0;

  /**
   * @brief Runs after the scene is processed and renders the primitives
   *
   * Once the scene is finished running it returns primitives that are then
   * rendered by the backend. It also needs to implement how ImGui is rendered
   * Runs once every frame
   */
  virtual void Render() = 0;  // Renders objects

  /**
   * @brief Callback that runs when an application is resized
   * @param width Width after resizing
   * @param height Height after resizing
   *
   * This needs to be modified with per-renderer implementation
   */
  virtual void OnResize(int32_t width, int32_t height);

  // Per-backend ImGui frame begin (e.g. ImGui_ImplDX11_NewFrame).
  virtual void ImGuiNewFrame() = 0;

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
