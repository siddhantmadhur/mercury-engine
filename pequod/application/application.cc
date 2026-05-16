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

static InputManager input_manager;

Application::Application(const std::string& window_title, float initial_width,
                         float initial_height) {
  PDebug::info("==================[ Pequod | {} | Windows ]==================",
               window_title);
  title_ = window_title;
  width_ = initial_width;
  height_ = initial_height;
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

  if (game_scene_) {
    game_scene_->SetWidth(width_);
    game_scene_->SetHeight(height_);
    game_scene_->SetInputManager(&input_manager);
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

  glfwSetKeyCallback(window_, HandleKeyCallback);
  glfwSetCursorPosCallback(
      window_, [](GLFWwindow* window, double xpos, double ypos) {
        input_manager.HandleCursorCallback(window, xpos, ypos);
      });

  glfwSetScrollCallback(
      window_, [](GLFWwindow* window, double xoffset, double yoffset) {
        input_manager.HandleScrollCallback(window, xoffset, yoffset);
      });

  glfwSetMouseButtonCallback(
      window_, [](GLFWwindow* win, int button, int action, int mods) {
        input_manager.HandleMouseButtonCallback(win, button, action, mods);
      });

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
  game_scene_->OnStart();

  GLFWcursor* cursor = nullptr;
  if (current_cursor) {
    cursor = glfwCreateCursor(current_cursor, 10, 8);
    if (cursor) {
      glfwSetCursor(window_, cursor);

    } else {
      PDebug::error("Cursor could not be created");
    }
  }
  while ((!glfwWindowShouldClose(window_)) && (!game_scene_->ShouldQuit())) {
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);

    double current_time = glfwGetTime() * 1000;
    auto frametime = current_time - time_elapsed_;
    delta_time_ = frametime;
    time_elapsed_ = current_time;

    {  // FPS logic
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

    constexpr double kTickMs = 1000.0 / kTicksPerSec;
    time_since_last_tick_ += delta_time_;

    int ticks = int(time_elapsed_ / kTickMs);

    if (game_scene_) {
      glfwPollEvents();
      ImGuiNewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      game_scene_->OnFrame(delta_time_);
      if (ticks > last_tick_) {
        game_scene_->OnTickBegin();
        game_scene_->OnTick(time_since_last_tick_);

        game_scene_->SimulatePhysics(
            60 / kTicksPerSec);  // Only works as long as tps is 60

        input_manager.ResetFreshPresses();

        last_tick_ = ticks;
        time_since_last_tick_ = 0.0;

        OnNewTick();
      }

      float alpha = static_cast<float>(time_since_last_tick_ / kTickMs);
      game_scene_->ProcessOnFrame(alpha);
      primitives_ = game_scene_->GetPrimitives();
    } else {
      PDebug::warn("Game scene not set");
    }

    Render();

    {
      if (!input_manager.IsHoveringOnUI()) {
        glfwSetCursor(window_, cursor);
      }
    }
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
  input_manager.HandleKeyCallback(window, key, scancode, action, mods);
}
void Application::SetPointer(std::string file_path) {
  PDebug::info("Setting cursor...");

  int x, y, channels;
  auto* res = stbi_load(file_path.c_str(), &x, &y, &channels, 4);
  if (res) {
    GLFWimage* image = new GLFWimage();
    image->width = x;
    image->height = y;
    image->pixels = res;
    this->current_cursor = image;

  } else {
    PDebug::error("Could not read file: {}", file_path);
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
