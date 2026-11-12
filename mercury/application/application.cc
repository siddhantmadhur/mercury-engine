//
// Created by smadhur on 4/5/2026.
//

#include "application.h"

#if defined(MERCURY_GRAPHICS_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>

#include "debugger/debugger.h"
#include "debugger/profiler.h"
#include "properties/texture2d.h"

namespace Mercury {

constexpr int kFrametimeSlidingWindowSize = 50;

static InputManager input_manager;

#if defined(_WIN32)
constexpr const char* kPlatformName = "Windows";
#elif defined(__linux__)
constexpr const char* kPlatformName = "Linux";
#elif defined(__APPLE__)
constexpr const char* kPlatformName = "macOS";
#else
constexpr const char* kPlatformName = "Unknown";
#endif

Application::Application(const std::string& window_title, float initial_width,
                         float initial_height) {
  PDebug::info("==================[ Mercury | {} | {} ]==================",
               window_title, kPlatformName);
  title_ = window_title;
  width_ = initial_width;
  height_ = initial_height;
}

bool Application::Initialize() {
  start_time_ = std::chrono::steady_clock::now();
  time_elapsed_ = 0.0;

  if (game_scene_) {
    game_scene_->SetWidth(width_);
    game_scene_->SetHeight(height_);
    game_scene_->SetInputManager(&input_manager);
    game_scene_->SetRenderHost(this);
  }

  if (headless_) {
    input_manager.Initialize(nullptr);
    return true;
  }

#if defined(MERCURY_GRAPHICS_VULKAN)
  glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
  if (!glfwInit()) {
    PDebug::error("Could not initialize glfw");
    return false;
  }

  GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* videoMode =
      primaryMonitor ? glfwGetVideoMode(primaryMonitor) : nullptr;
  if (videoMode == nullptr) {
    PDebug::warn("Could not query glfw video mode");
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

  if (videoMode != nullptr) {
    const int32_t windowLeft = videoMode->width / 2 - width_ / 2;
    const int32_t windowTop = videoMode->height / 2 - height_ / 2;
    glfwSetWindowPos(window_, windowLeft, windowTop);
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetFramebufferSizeCallback(window_, HandleResize);
  input_manager.Initialize(window_);

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

  glfwSetCharCallback(window_, [](GLFWwindow* win, unsigned int codepoint) {
    input_manager.HandleCharCallback(win, codepoint);
  });

  glfwSetCursorEnterCallback(window_, [](GLFWwindow* win, int entered) {
    input_manager.HandleCursorEnterCallback(win, entered);
  });

  return true;
}

void Application::StepFrame() {
  if (!game_scene_) return;
  if (!IsDeviceReady()) {
    if (!headless_) glfwPollEvents();
    return;
  }

  Profiler::BeginFrame();
  const auto now = std::chrono::steady_clock::now();
  const double current_time =
      std::chrono::duration<double, std::milli>(now - start_time_).count();
  auto frametime = current_time - time_elapsed_;
  delta_time_ = frametime;
  // Scripted input and benchmarks replay the same way however fast frames
  // render.
  if (!input_script_.IsEmpty()) delta_time_ = 1000.0 / 60.0;
  if (Profiler::FixedDeltaMs() > 0.0) delta_time_ = Profiler::FixedDeltaMs();
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
    average_fps_ =
        average > 0.0 ? static_cast<int>(std::round(1000.0 / average)) : 0;
  }

  const int tick_rate = std::max(1, game_scene_->GetTicksPerSecond());
  const double kTickMs = 1000.0 / tick_rate;
  time_since_last_tick_ += delta_time_;

  if (!headless_) {
    input_manager.BeginFrame();
    glfwPollEvents();
    int window_w = 0, window_h = 0, fb_w = 0, fb_h = 0;
    glfwGetWindowSize(window_, &window_w, &window_h);
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    if (window_w > 0 && window_h > 0) {
      input_manager.SetFramebufferScale(
          glm::vec2(float(fb_w) / window_w, float(fb_h) / window_h));
    }
    float content_x = 1.0f, content_y = 1.0f;
    glfwGetWindowContentScale(window_, &content_x, &content_y);
    input_manager.SetContentScale(content_x);
    input_manager.PollGamepads();
  }
  if (!input_script_.IsEmpty()) {
    InputScript::Hooks hooks;
    hooks.capture = [this](const std::string& file) { RequestCapture(file); };
    hooks.quit = [this] { quit_after_frame_ = true; };
    input_script_.Apply(script_frame_++, input_manager, window_, hooks);
  }

  {
    MERCURY_PROFILE_SCOPE("update");
    game_scene_->OnFrame(delta_time_);
  }
  // Run every tick that is due (Unity runs several FixedUpdates when a frame
  // takes long), but never more than a few so a stall cannot snowball.
  int ticks_run = 0;
  while (time_since_last_tick_ >= kTickMs && ticks_run < 5) {
    game_scene_->OnTickBegin();
    game_scene_->OnTick(kTickMs);
    game_scene_->SimulatePhysics(1);
    input_manager.ResetFreshPresses();
    time_since_last_tick_ -= kTickMs;
    ticks_run++;
  }
  if (time_since_last_tick_ > kTickMs) time_since_last_tick_ = std::fmod(time_since_last_tick_, kTickMs);

  float alpha = static_cast<float>(time_since_last_tick_ / kTickMs);
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  {
    MERCURY_PROFILE_SCOPE("process_on_frame");
    game_scene_->ProcessOnFrame(alpha);
  }
  // Rebuild the per-frame vertex/index buffers to match the primitives_ we
  // are about to render.
  {
    MERCURY_PROFILE_SCOPE("new_tick");
    OnNewTick();
  }

  // A minimized window has a 0x0 framebuffer, which most backends cannot create
  // swapchains or targets for.
  if (!headless_ && window_) {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width == 0 || framebuffer_height == 0) {
      glfwWaitEventsTimeout(0.05);
      return;
    }
  }

  // The benchmark is done: a screenshot of this frame if asked for, then quit.
  if (Profiler::BenchFinished() && !quit_after_frame_) {
    if (const char* capture = std::getenv("MERCURY_BENCH_CAPTURE")) RequestCapture(capture);
    quit_after_frame_ = true;
  }
  {
    MERCURY_PROFILE_SCOPE("render");
    Render();
  }
  Profiler::EndFrame();
  if (headless_) {
    // Hosted UI (e.g. wxWidgets) injects input between StepFrame() calls; clear
    // per-frame deltas only after this frame has finished consuming them.
    input_manager.BeginFrame();
  }
  if (quit_after_frame_) Quit();
}

void Application::RequestCapture(const std::string& file) {
  PDebug::warn("This renderer cannot save captures ({})", file);
}

bool Application::StartHosted() {
  if (!Initialize()) {
    PDebug::error("Could not initialize application");
    return false;
  }

  if (!OnLoad()) {
    PDebug::error("Could not load application");
    return false;
  }
  if (game_scene_) {
    game_scene_->OnStart();
  }

  if (const char* script = std::getenv("MERCURY_INPUT_SCRIPT")) {
    std::string error;
    if (input_script_.Load(script, &error)) {
      PDebug::info("Replaying input from {}", script);
    } else {
      PDebug::error("Input script: {}", error);
    }
  }
  return true;
}

int Application::Run() {
  if (!StartHosted()) {
    return 1;
  }

  if (!headless_ && window_ && current_cursor) {
    GLFWcursor* cursor = glfwCreateCursor(current_cursor, 10, 8);
    if (cursor) {
      glfwSetCursor(window_, cursor);
    } else {
      PDebug::error("Cursor could not be created");
    }
  }

  while (game_scene_ && !game_scene_->ShouldQuit()) {
    if (!headless_ && window_ && glfwWindowShouldClose(window_)) {
      if (game_scene_->OnCloseRequested()) break;
      glfwSetWindowShouldClose(window_, GLFW_FALSE);
    }
    StepFrame();
  }
  return 0;
}

Application::~Application() {
  PDebug::info("Application shutting down...");
  if (!headless_) {
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
  }
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
  auto* res = LoadImageFile(file_path, &x, &y, &channels, 4);
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
void Application::Quit() const {
  if (window_) glfwSetWindowShouldClose(window_, true);
  else if (game_scene_) game_scene_->QuitScene();
}
void Application::SetWindowTitle(const std::string& title) {
  title_ = title;
  if (window_) glfwSetWindowTitle(window_, title.c_str());
  if (title_callback_) title_callback_(title);
}
void Application::SetGameScene(std::unique_ptr<GameScene> game_scene) {
  this->game_scene_ = std::move(game_scene);
  if (game_scene_) {
    game_scene_->SetWidth(width_);
    game_scene_->SetHeight(height_);
    game_scene_->SetInputManager(&input_manager);
    game_scene_->SetRenderHost(this);
  }
}

InputManager& Application::GetInputManager() { return input_manager; }

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

}  // namespace Mercury
