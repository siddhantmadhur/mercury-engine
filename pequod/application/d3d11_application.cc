//
// Created by smadhur on 4/5/2026.
//

#include "d3d11_application.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <DirectXMath.h>
#include <GLFW/glfw3native.h>
#include <d3dcompiler.h>

#include <filesystem>
#include <format>
#include <string>

#include "debugger/debugger.h"
#include "os/filesystem.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxguid.lib")

using std::string;

namespace Pequod {
D3D11Application::D3D11Application(const std::string& window_title)
    : Application(window_title) {}

bool D3D11Application::Initialize() {
  if (!Application::Initialize()) {
    PDebug::error("Could not initialize GLFW application");
    return false;
  }

  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_)))) {
    PDebug::error("Could not create DXGIFactory...");
    return false;
  }

  // Create dxgi factory
  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
  constexpr D3D_FEATURE_LEVEL deviceFeatureLevel =
      D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(
          nullptr, D3D_DRIVER_TYPE::D3D_DRIVER_TYPE_HARDWARE, nullptr,
          creationFlags, &deviceFeatureLevel, 1, D3D11_SDK_VERSION, &device_,
          nullptr, &deviceContext_))) {
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
  swapChainDescriptor.SwapEffect =
      DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDescriptor.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
  swapChainDescriptor.Flags = {};
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDescriptor = {};
  swapChainFullscreenDescriptor.Windowed = true;

  auto win32Window = glfwGetWin32Window(GetWindow());
  if (win32Window == nullptr) {
    PDebug::error("Could not get win32 from glfw");
    return false;
  }
  if (FAILED(dxgiFactory_->CreateSwapChainForHwnd(
          device_.Get(), win32Window, &swapChainDescriptor,
          &swapChainFullscreenDescriptor, nullptr, &swapchain_))) {
    PDebug::error("DXGI: Could not create swapchain");
    return false;
  }

  if (!CreateSwapchainResources()) {
    PDebug::error("Could not create swapchain resource");
    return false;
  }

  return true;
}

bool D3D11Application::CreateSwapchainResources() {
  ComPtr<ID3D11Texture2D> backBuffer = nullptr;
  if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
    PDebug::error("D3D11: Failed to get back buffer");
    return false;
  }

  if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                             &renderTarget_))) {
    PDebug::error("D3D11: Failed to create RTV");
    return false;
  }

  return true;
}

void D3D11Application::DestroySwapchainResources() { renderTarget_.Reset(); }

bool D3D11Application::OnLoad() {
#ifndef PEQUOD_SHADER_PATH
  PDebug::error("Could not locate header: PEQUOD_SHADER_PATH");
  return false;
#endif

  fs::path shader_path = PEQUOD_SHADER_PATH;

  fs::path vertex_shader_path = shader_path / "main.vs.hlsl";
  fs::path pixel_shader_path = shader_path / "main.ps.hlsl";

  ComPtr<ID3DBlob> vertexShaderBlob = nullptr;
  vertexShader_ = CreateVertexShader(vertex_shader_path, vertexShaderBlob);
  if (vertexShader_ == nullptr) {
    return false;
  }

  pixelShader_ = CreatePixelShader(pixel_shader_path);
  if (pixelShader_ == nullptr) {
    return false;
  }

  constexpr D3D11_INPUT_ELEMENT_DESC vertexInputLayoutInfo[] = {
      {"POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT, 0,
       offsetof(VertexPositionColor, position),
       D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT, 0,
       offsetof(VertexPositionColor, color),
       D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  if (FAILED(device_->CreateInputLayout(
          vertexInputLayoutInfo, _countof(vertexInputLayoutInfo),
          vertexShaderBlob->GetBufferPointer(),
          vertexShaderBlob->GetBufferSize(), &vertexLayout_))) {
    PDebug::error("D3D11: Failed to create default vertex input layout");
    return false;
  }

  constexpr VertexPositionColor vertices[] = {
      {Position{0.0f, 0.5f, 0.0f}, Color{0.25f, 0.39f, 0.19f}},
      {Position{0.5f, -0.5f, 0.0f}, Color{0.44f, 0.75f, 0.35f}},
      {Position{-0.5f, -0.5f, 0.0f}, Color{0.38f, 0.55f, 0.20f}},
  };

  D3D11_BUFFER_DESC bufferInfo = {};
  bufferInfo.ByteWidth = sizeof(vertices);
  bufferInfo.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
  bufferInfo.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;

  D3D11_SUBRESOURCE_DATA resourceData = {};
  resourceData.pSysMem = vertices;
  if (FAILED(device_->CreateBuffer(&bufferInfo, &resourceData,
                                   &triangleVertices_))) {
    PDebug::error("D3D11: Failed to create triangle vertex buffer");
    return false;
  }

  return true;
}

void D3D11Application::Render() {
  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.Width = static_cast<float>(GetWidth());
  viewport.Height = static_cast<float>(GetHeight());
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  constexpr float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
  constexpr UINT vertexStride = sizeof(VertexPositionColor);
  constexpr UINT vertexOffset = 0;

  deviceContext_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
  deviceContext_->IASetInputLayout(vertexLayout_.Get());
  deviceContext_->IASetVertexBuffers(0, 1, triangleVertices_.GetAddressOf(),
                                     &vertexStride, &vertexOffset);
  deviceContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deviceContext_->VSSetShader(vertexShader_.Get(), nullptr, 0);

  deviceContext_->RSSetViewports(1, &viewport);
  deviceContext_->PSSetShader(pixelShader_.Get(), nullptr, 0);
  deviceContext_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
  deviceContext_->Draw(3, 0);
  swapchain_->Present(1, 0);
}

void D3D11Application::UpdateOnFrame() {}

void D3D11Application::UpdateOnTick() {}

void D3D11Application::OnResize(int32_t width, int32_t height) {
  Application::OnResize(width, height);
  deviceContext_->Flush();
  DestroySwapchainResources();

  if (FAILED(swapchain_->ResizeBuffers(
          0, width, height, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0))) {
    PDebug::error("D3D11: Failed to recreate SwapChain buffers");
    return;
  }

  CreateSwapchainResources();
}

// Shader stuff

bool D3D11Application::CompileShader(const std::wstring& fileName,
                                     const std::string& entryPoint,
                                     const std::string& profile,
                                     ComPtr<ID3DBlob>& shaderBlob) {
  constexpr UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;

  ComPtr<ID3DBlob> tempShaderBlob = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;

  if (FAILED(D3DCompileFromFile(fileName.data(), nullptr,
                                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                entryPoint.data(), profile.data(), compileFlags,
                                0, &tempShaderBlob, &errorBlob))) {
    PDebug::error("D3D11: Failed to read shader from file");
    if (errorBlob != nullptr) {
      PDebug::error("D3D11: MESSAGE: ");
      PDebug::error("{}",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    return false;
  }

  shaderBlob = std::move(tempShaderBlob);
  PDebug::info("Compiled shader!");
  return true;
}

D3D11Application::ComPtr<ID3D11VertexShader>
D3D11Application::CreateVertexShader(const std::wstring& fileName,
                                     ComPtr<ID3DBlob>& vertexShaderBlob) const {
  if (!CompileShader(fileName, "Main", "vs_5_0", vertexShaderBlob)) {
    return nullptr;
  }

  ComPtr<ID3D11VertexShader> vertexShader;
  if (FAILED(device_->CreateVertexShader(vertexShaderBlob->GetBufferPointer(),
                                         vertexShaderBlob->GetBufferSize(),
                                         nullptr, &vertexShader))) {
    PDebug::error("D3D11: Failed to compile vertex shader");
    return nullptr;
  }

  return vertexShader;
}

D3D11Application::ComPtr<ID3D11PixelShader> D3D11Application::CreatePixelShader(
    const std::wstring& fileName) const {
  ComPtr<ID3DBlob> pixelShaderBlob = nullptr;
  if (!CompileShader(fileName, "Main", "ps_5_0", pixelShaderBlob)) {
    return nullptr;
  }

  ComPtr<ID3D11PixelShader> pixelShader;
  if (FAILED(device_->CreatePixelShader(pixelShaderBlob->GetBufferPointer(),
                                        pixelShaderBlob->GetBufferSize(),
                                        nullptr, &pixelShader))) {
    PDebug::error("D3D11: Failed to compile pixel shader");
    return nullptr;
  }

  return pixelShader;
}

}  // namespace Pequod
