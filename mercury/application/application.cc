//
// Created by smadhur on 4/5/2026.
//

#include "application.h"

#if defined(MERCURY_GRAPHICS_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>

#include <cstdlib>
#include <format>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

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
#if defined(MERCURY_GRAPHICS_VULKAN)
  glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
  if (!glfwInit()) {
    PDebug::error("Could not initialize glfw");
    return false;
  }

#if defined(__EMSCRIPTEN__)
  double css_width = EM_ASM_DOUBLE({
    var c = document.getElementById('canvas');
    if (c) {
      c.style.removeProperty('width');
      c.style.removeProperty('height');
    }
    return window.innerWidth || 1280.0;
  });
  double css_height = EM_ASM_DOUBLE({
    return window.innerHeight || 720.0;
  });
  if (css_width > 0.0 && css_height > 0.0) {
    double dpr = EM_ASM_DOUBLE({
      return Math.min(window.devicePixelRatio || 1.0, 2.0);
    });
    width_ = static_cast<float>(std::round(css_width * dpr));
    height_ = static_cast<float>(std::round(css_height * dpr));
    emscripten_set_canvas_element_size("#canvas", static_cast<int>(width_),
                                       static_cast<int>(height_));
  }
#else
  GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* videoMode =
      primaryMonitor ? glfwGetVideoMode(primaryMonitor) : nullptr;
  if (videoMode == nullptr) {
    PDebug::warn("Could not query glfw video mode");
  }
#endif

  if (game_scene_) {
    game_scene_->SetWidth(width_);
    game_scene_->SetHeight(height_);
    game_scene_->SetInputManager(&input_manager);
    game_scene_->SetRenderHost(this);
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

#if defined(__EMSCRIPTEN__)
  EM_ASM({
    var c = document.getElementById('canvas');
    if (c) {
      c.style.removeProperty('width');
      c.style.removeProperty('height');
    }
    if (typeof Browser !== 'undefined') {
      Browser.resizeCanvas = true;
    }
  });
#else
  if (videoMode != nullptr) {
    const int32_t windowLeft = videoMode->width / 2 - width_ / 2;
    const int32_t windowTop = videoMode->height / 2 - height_ / 2;
    glfwSetWindowPos(window_, windowLeft, windowTop);
  }
#endif

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

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // The backend initializes the ImGui GLFW platform layer (e.g.
  // ImGui_ImplGlfw_InitForVulkan) once its device exists.

  return true;
}

void Application::StepFrame() {
  CheckCanvasResize();
  if (!game_scene_) return;
  if (!IsDeviceReady()) {
    glfwPollEvents();
    return;
  }

  Profiler::BeginFrame();
  double current_time = glfwGetTime() * 1000;
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
    average_fps_ = std::round(1000 / average);
  }

  const int tick_rate = std::max(1, game_scene_->GetTicksPerSecond());
  const double kTickMs = 1000.0 / tick_rate;
  time_since_last_tick_ += delta_time_;

  input_manager.BeginFrame();
  glfwPollEvents();
  {
    int window_w = 0, window_h = 0, fb_w = 0, fb_h = 0;
    glfwGetWindowSize(window_, &window_w, &window_h);
    glfwGetFramebufferSize(window_, &fb_w, &fb_h);
    if (window_w > 0 && window_h > 0) {
      input_manager.SetFramebufferScale(
          glm::vec2(float(fb_w) / window_w, float(fb_h) / window_h));
    }
#if !defined(__EMSCRIPTEN__)
    float content_x = 1.0f, content_y = 1.0f;
    glfwGetWindowContentScale(window_, &content_x, &content_y);
    input_manager.SetContentScale(content_x);
#endif
  }
  input_manager.PollGamepads();
  if (!input_script_.IsEmpty()) {
    InputScript::Hooks hooks;
    hooks.capture = [this](const std::string& file) { RequestCapture(file); };
    hooks.quit = [this] { quit_after_frame_ = true; };
    input_script_.Apply(script_frame_++, input_manager, window_, hooks);
  }
  ImGuiNewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  {
    const ImGuiIO& io = ImGui::GetIO();
    input_manager.SetImGuiCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
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
  // swapchains or targets for. Close out the ImGui frame without drawing.
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
  if (framebuffer_width == 0 || framebuffer_height == 0) {
    ImGui::EndFrame();
#if !defined(__EMSCRIPTEN__)
    glfwWaitEventsTimeout(0.05);
#endif
    return;
  }

  Profiler::DrawOverlay();
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
  if (quit_after_frame_) Quit();
}

void Application::RequestCapture(const std::string& file) {
  PDebug::warn("This renderer cannot save captures ({})", file);
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

#if !defined(__EMSCRIPTEN__)
  if (const char* script = std::getenv("MERCURY_INPUT_SCRIPT")) {
    std::string error;
    if (input_script_.Load(script, &error)) {
      PDebug::info("Replaying input from {}", script);
    } else {
      PDebug::error("Input script: {}", error);
    }
  }
#endif

  if (current_cursor) {
    GLFWcursor* cursor = glfwCreateCursor(current_cursor, 10, 8);
    if (cursor) {
      glfwSetCursor(window_, cursor);
    } else {
      PDebug::error("Cursor could not be created");
    }
  }

#if defined(__EMSCRIPTEN__)
  emscripten_set_main_loop_arg(
      [](void* arg) { static_cast<Application*>(arg)->StepFrame(); }, this, 0,
      1);
  return 0;
#else
  while (!game_scene_->ShouldQuit()) {
    if (glfwWindowShouldClose(window_)) {
      if (game_scene_->OnCloseRequested()) break;
      glfwSetWindowShouldClose(window_, GLFW_FALSE);
    }
    StepFrame();
  }
  return 0;
#endif
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
void Application::Quit() const { glfwSetWindowShouldClose(window_, true); }
void Application::SetWindowTitle(const std::string& title) {
  title_ = title;
  if (window_) glfwSetWindowTitle(window_, title.c_str());
}
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

void Application::CheckCanvasResize() {
#if defined(__EMSCRIPTEN__)
  double css_width = EM_ASM_DOUBLE({
    var c = document.getElementById('canvas');
    if (c) {
      c.style.removeProperty('width');
      c.style.removeProperty('height');
    }
    return window.innerWidth || (c ? c.clientWidth : 0);
  });
  double css_height = EM_ASM_DOUBLE({
    return window.innerHeight || (document.getElementById('canvas') ? document.getElementById('canvas').clientHeight : 0);
  });

  if (css_width <= 0.0 || css_height <= 0.0) {
    return;
  }

  double dpr = EM_ASM_DOUBLE({
    return Math.min(window.devicePixelRatio || 1.0, 2.0);
  });

  int target_width = static_cast<int>(std::round(css_width * dpr));
  int target_height = static_cast<int>(std::round(css_height * dpr));

  int cur_canvas_w = EM_ASM_INT({
    var c = document.getElementById('canvas');
    return c ? c.width : 0;
  });
  int cur_canvas_h = EM_ASM_INT({
    var c = document.getElementById('canvas');
    return c ? c.height : 0;
  });

  if (target_width > 0 && target_height > 0 &&
      (target_width != cur_canvas_w || target_height != cur_canvas_h ||
       target_width != static_cast<int>(width_) ||
       target_height != static_cast<int>(height_))) {
    emscripten_set_canvas_element_size("#canvas", target_width, target_height);
    EM_ASM({
      if (typeof GLFW !== 'undefined' && GLFW.active) {
        GLFW.active.framebufferWidth = $0;
        GLFW.active.framebufferHeight = $1;
        GLFW.active.width = $2;
        GLFW.active.height = $3;
      }
      if (typeof Browser !== 'undefined' && Browser.updateResizeListeners) {
        Browser.updateResizeListeners();
      }
    }, target_width, target_height, static_cast<int>(std::round(css_width)), static_cast<int>(std::round(css_height)));

    OnResize(target_width, target_height);
  }
#endif
}

}  // namespace Mercury
