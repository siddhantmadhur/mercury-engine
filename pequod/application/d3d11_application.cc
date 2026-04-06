//
// Created by smadhur on 4/5/2026.
//

#include "d3d11_application.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <format>

#include "debugger/debugger.hh"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxguid.lib")

namespace Pequod
{
    D3D11Application::D3D11Application(const std::string& window_title)
    : Application(window_title)
    {
    }

    bool D3D11Application::Initialize()
    {
        if (!Application::Initialize())
        {
            PDebug::error("Could not initialize GLFW application");
            return false;
        }

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_))))
        {
            PDebug::error("Could not create DXGIFactory...");
            return false;
        }

        // Create dxgi factory
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
        constexpr D3D_FEATURE_LEVEL deviceFeatureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            &deviceFeatureLevel,
            1,
            D3D11_SDK_VERSION,
            &device_,
            nullptr,
            &deviceContext_)))
        {
            PDebug::error("D3D11: Failed to create device");
            return false;
        }

        // create device and device context
        DXGI_SWAP_CHAIN_DESC1 swapChainDescriptor = {};
        swapChainDescriptor.Width = GetWidth();
        swapChainDescriptor.Height = GetHeight();
        swapChainDescriptor.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDescriptor.SampleDesc.Count = 1;
        swapChainDescriptor.SampleDesc.Quality = 0;
        swapChainDescriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescriptor.BufferCount = 2;
        swapChainDescriptor.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDescriptor.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
        swapChainDescriptor.Flags = {};
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDescriptor = {};
        swapChainFullscreenDescriptor.Windowed = true;

        auto win32Window = glfwGetWin32Window(GetWindow());
        if (win32Window == nullptr)
        {
            PDebug::error("Could not get win32 from glfw");
            return false;
        }
        if (FAILED(dxgiFactory_->CreateSwapChainForHwnd(
            device_.Get(),
            win32Window,
            &swapChainDescriptor,
            &swapChainFullscreenDescriptor,
            nullptr,
            &swapchain_)))
        {
            PDebug::error("DXGI: Could not create swapchain");
            return false;
        }

        if (!CreateSwapchainResources())
        {
            PDebug::error("Could not create swapchain resource");
            return false;
        }

        return true;
    }

    bool D3D11Application::CreateSwapchainResources()
    {
        ComPtr<ID3D11Texture2D> backBuffer = nullptr;
        if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        {
            PDebug::error("D3D11: Failed to get back buffer");
            return false;
        }

        if (FAILED(device_->CreateRenderTargetView(
            backBuffer.Get(), nullptr, &renderTarget_)))
        {
            PDebug::error("D3D11: Failed to create RTV");
            return false;
        }

        return true;
    }

    void D3D11Application::DestroySwapchainResources()
    {
        renderTarget_.Reset();
    }

    bool D3D11Application::OnLoad()
    {
        return true;
    }

    void D3D11Application::Render()
    {
        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = static_cast<float>(GetWidth());
        viewport.Height = static_cast<float>(GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        constexpr float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };

        deviceContext_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
        deviceContext_->RSSetViewports(1, &viewport);
        deviceContext_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        swapchain_->Present(1, 0);
    }

    void D3D11Application::UpdateOnFrame()
    {
    }

    void D3D11Application::UpdateOnTick()
    {

    }

    void D3D11Application::OnResize(int32_t width, int32_t height)
    {
        Application::OnResize(width, height);
        deviceContext_->Flush();
        DestroySwapchainResources();

        if (FAILED(swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT::DXGI_FORMAT_B8G8R8X8_UNORM, 0)))
        {
            PDebug::error("D3D11: Failed to recreate SwapChain buffers");
            return;
        }

        CreateSwapchainResources();
    }
}
