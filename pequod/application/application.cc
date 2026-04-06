//
// Created by smadhur on 4/5/2026.
//

#include "application.h"

#include <GLFW/glfw3.h>

#include "debugger/debugger.hh"
#include <format>
namespace Pequod
{
    Application::Application(const std::string& window_title)
    {
        PDebug::info("Application context created...");
        title_ = window_title;

    }

    bool Application::Initialize()
    {
        PDebug::info("Initializing...");

        if (!glfwInit())
        {
            PDebug::error("Could not initialize glfw");
        }

        GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
        width_ = static_cast<int32_t>(videoMode->width * 0.6f);
        height_ = static_cast<int32_t>(videoMode->height * 0.6f);

        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        window_ = glfwCreateWindow(
            width_,
            height_,
            title_.data(),
            nullptr,
            nullptr);
        if (window_ == nullptr)
        {
            PDebug::error("GLFW: could not create window.");
            glfwTerminate();
            return false;
        }

        const int32_t windowLeft = videoMode->width / 2 - width_ / 2;
        const int32_t windowTop = videoMode->height / 2 - height_ / 2;
        glfwSetWindowPos(window_, windowLeft, windowTop);

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, HandleResize);

        return true;
    }

    int Application::Run()
    {

        if (!Initialize())
        {
            PDebug::error("Could not initialize application");
            return 1;
        }

        if (!OnLoad())
        {
            PDebug::error("Could not load application");
            return 1;
        }

        while (!glfwWindowShouldClose(window_))
        {
            glfwPollEvents();
            UpdateOnTick(); // Run as many ticks as possible (i.e. 1 per frame)

            UpdateOnFrame();
            Render();
        }

        return 0;
    }

    Application::~Application()
    {
        PDebug::info("Application shutting down...");
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void Application::HandleResize(GLFWwindow* window, int32_t width, int32_t height)
    {
        PDebug::info(std::format("Resizing window: {}-{}", width, height));
        auto application = static_cast<Application*>(glfwGetWindowUserPointer(window));
        application->OnResize(width, height);
    }

    const int32_t Application::GetHeight()
    {
        return height_;
    }

    const int32_t Application::GetWidth()
    {
        return width_;
    }

    GLFWwindow* Application::GetWindow()
    {
        return window_;
    }

    void Application::OnResize(int32_t width, int32_t height)
    {
        width_ = width;
        height_ = height;
    }
}
