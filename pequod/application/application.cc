//
// Created by smadhur on 4/5/2026.
//

#include "application.h"

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include <format>

#include "debugger/debugger.h"

namespace Pequod {

constexpr int kFrametimeSlidingWindowSize = 50;

// static InputManager input_manager;

static void GLFWHandleCursorPositionCallback(GLFWwindow* window, double xpos,
                                             double ypos) {
  Application* application =
      static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application) {
    // application->HandleCursorPositionCallback(xpos, ypos);
    auto input_manager = application->GetInputManager();
    input_manager->HandleCursorPositionCallback(xpos, ypos);
  }
}

static void GLFWHandleScrollCallback(GLFWwindow* window, double xoffset,
                                     double yoffset) {
  Application* application =
      static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application) {
    auto input_manager = application->GetInputManager();
    input_manager->HandleScrollCallback(xoffset, yoffset);
  }
}

static void GLFWHandleMouseButtonCallback(GLFWwindow* window, int button,
                                          int action, int mods) {
  Application* application =
      static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application) {
    auto input_manager = application->GetInputManager();
    input_manager->HandleMouseButtonCallback(button, action, mods);
  }
}

static void GLFWHandleKeyboardCallback(GLFWwindow* window, int key,
                                       int scancode, int action, int mods) {
  Application* application =
      static_cast<Application*>(glfwGetWindowUserPointer(window));
  if (application) {
    auto input_manager = application->GetInputManager();
    input_manager->HandleKeyCallback(key, scancode, action, mods);
  }
}

Application::Application(const std::string& window_title, float initial_width,
                         float initial_height) {
  PDebug::info("==================[ Pequod | {} | Windows ]==================",
               window_title);
  title_ = window_title;
  width_ = initial_width;
  height_ = initial_height;

  scene_manager_ = std::make_shared<SceneManager>(this);
  input_manager_ = std::make_shared<InputManager>(this);
}

SPtr<InputManager> Application::GetInputManager() {
  return this->input_manager_;
}
SPtr<SceneManager> Application::GetSceneManager() {
  return this->scene_manager_;
}

bool Application::Initialize() {
  if (!glfwInit()) {
    PDebug::error("Could not initialize glfw");
  }

  GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
  if (videoMode == nullptr) {
    PDebug::error("Could not create glfw video mode");
    return false;
  }

  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  // glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

  PDebug::info("Creating window: {}", title_);
  window_ = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, nullptr);
  if (window_ == nullptr) {
    PDebug::error("GLFW: could not create window.");
    glfwTerminate();
    return false;
  }

  const int32_t windowLeft = videoMode->width / 2 - width_ / 2;
  const int32_t windowTop = videoMode->height / 2 - height_ / 2;
  glfwSetWindowPos(window_, windowLeft, windowTop);

  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, HandleResize);

  glfwSetKeyCallback(window_, GLFWHandleKeyboardCallback);
  glfwSetCursorPosCallback(window_, GLFWHandleCursorPositionCallback);
  glfwSetScrollCallback(window_, GLFWHandleScrollCallback);
  glfwSetMouseButtonCallback(window_, GLFWHandleMouseButtonCallback);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();

  ImGui_ImplGlfw_InitForOther(window_, true);

  return true;
}

int Application::Run() {
  if (!Initialize()) {
    PDebug::error("Could not initialize application");
    return 1;
  }

  if (!OnLoad()) {
    PDebug::error("Could not load application");
    return 1;
  }

  while ((!glfwWindowShouldClose(window_))) {
    glfwPollEvents();

    double current_time = glfwGetTime() * 1000;
    auto frametime = current_time - time_elapsed_;
    delta_time_ = frametime;
    time_elapsed_ = current_time;

    if constexpr (kShowDebugStats) {  // FPS logic
      fps_sliding_window_.push_front(frametime);

      if (fps_sliding_window_.size() > kFrametimeSlidingWindowSize) {
        fps_sliding_window_.pop_back();
      }

      double total = 0;
      for (int i = 0; i < fps_sliding_window_.size(); i++) {
        total += fps_sliding_window_[i];
      }
      double average = total / fps_sliding_window_.size();
      average_fps_ = std::round(1000 / average);
    }

    time_since_last_tick_ += delta_time_;

    int ticks = int(time_elapsed_ / kTickMs);

    float alpha = static_cast<float>(time_since_last_tick_ / kTickMs);

    OnNewTick();

    PrepareRenderPass();
    Render();
    glfwSwapBuffers(window_);
  }

  return 0;
}

Application::~Application() {
  PDebug::info("Application shutting down...");
  glfwDestroyWindow(window_);
  glfwTerminate();
}

void Application::HandleResize(GLFWwindow* window, int32_t width,
                               int32_t height) {
  PDebug::info("Resizing window: {}-{}", width, height);
  auto application =
      static_cast<Application*>(glfwGetWindowUserPointer(window));
  application->OnResize(width, height);
}
void Application::Quit() const { glfwSetWindowShouldClose(window_, true); }

int32_t Application::GetHeight() const { return height_; }

int32_t Application::GetWidth() const { return width_; }

GLFWwindow* Application::GetWindow() const { return window_; }

void Application::OnResize(int32_t width, int32_t height) {
  width_ = width;
  height_ = height;
}
}  // namespace Pequod
