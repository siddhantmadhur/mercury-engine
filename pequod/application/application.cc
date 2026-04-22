//
// Created by smadhur on 4/5/2026.
//

#include "application.h"

#include <GLFW/glfw3.h>

#include <format>

#include "debugger/debugger.h"

namespace Pequod {

static InputManager input_manager;

Application::Application(const std::string& window_title) {
  PDebug::info("Application context created...");
  title_ = window_title;
}

bool Application::Initialize() {
  PDebug::info("Initializing...");

  if (!glfwInit()) {
    PDebug::error("Could not initialize glfw");
  }

  GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
  if (videoMode == nullptr) {
    PDebug::error("Could not create glfw video mode");
    return false;
  }
  width_ = static_cast<int32_t>(videoMode->width * 0.6f);
  height_ = static_cast<int32_t>(videoMode->height * 0.6f);

  if (game_scene_) {
    game_scene_->SetWidth(width_);
    game_scene_->SetHeight(height_);
    game_scene_->SetInputManager(&input_manager);
  }

  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

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

  glfwSetKeyCallback(window_, HandleKeyCallback);

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

  game_scene_->OnStart();

  while ((!glfwWindowShouldClose(window_)) && (!game_scene_->ShouldQuit())) {
    double current_time = glfwGetTime() * 1000;
    delta_time_ = current_time - time_elapsed_;
    time_elapsed_ = current_time;
    auto ticks_per_sec = 60;
    time_since_last_tick_ += delta_time_;

    int ticks = int(time_elapsed_ / (1000.0 / ticks_per_sec));

    glfwPollEvents();

    if (game_scene_) {
      if (ticks > last_tick_) {
        last_tick_ = ticks;
        game_scene_->OnTick(time_since_last_tick_);
        time_since_last_tick_ = 0.0f;
        game_scene_->SimulatePhysics();  // Only works as long as tps is 60
      }
      game_scene_->OnFrame(delta_time_);
    } else {
      PDebug::warn("Game scene not set");
    }

    Render();
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
void Application::HandleKeyCallback(GLFWwindow* window, int key, int scancode,
                                    int action, int mods) {
  if (action == GLFW_PRESS) {
    input_manager.SetKeyDown(key);
  } else if (action == GLFW_RELEASE) {
    input_manager.SetKeyUp(key);
  }
}
void Application::Quit() const { glfwSetWindowShouldClose(window_, true); }
void Application::SetGameScene(std::unique_ptr<GameScene> game_scene) {
  this->game_scene_ = std::move(game_scene);
}

int32_t Application::GetHeight() const { return height_; }

int32_t Application::GetWidth() const { return width_; }

GLFWwindow* Application::GetWindow() const { return window_; }

void Application::OnResize(int32_t width, int32_t height) {
  width_ = width;
  height_ = height;
  if (game_scene_) {
    game_scene_->SetWidth(width);
    game_scene_->SetHeight(height);
  }
}
}  // namespace Pequod
