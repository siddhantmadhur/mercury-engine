//
// Created by smadhur on 4/5/2026.
//
#include "d3d11_application.h"

#include <imgui/imgui.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>

#include <DirectXMath.h>
#include <GLFW/glfw3native.h>
#include <d3dcompiler.h>
#include <imgui/backends/imgui_impl_dx11.h>

#include <filesystem>
#include <format>
#include <string>

#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "imgui/backends/imgui_impl_glfw.h"
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

  ImGui_ImplDX11_Init(device_.Get(), deviceContext_.Get());

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
       offsetof(Vertex, position),
       D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT, 0,
       offsetof(Vertex, color),
       D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0},
  };

  if (FAILED(device_->CreateInputLayout(
          vertexInputLayoutInfo, _countof(vertexInputLayoutInfo),
          vertexShaderBlob->GetBufferPointer(),
          vertexShaderBlob->GetBufferSize(), &vertexLayout_))) {
    PDebug::error("D3D11: Failed to create default vertex input layout");
    return false;
  }

  primitives_ = game_scene_->GetPrimitives();

  D3D11_BUFFER_DESC bufferInfo = {};
  bufferInfo.ByteWidth = sizeof(Vertex) * 64000;  // Max no. of vertices
  bufferInfo.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
  bufferInfo.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
  bufferInfo.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  D3D11_SUBRESOURCE_DATA resourceData = {};
  if (FAILED(device_->CreateBuffer(&bufferInfo, nullptr, &triangleVertices_))) {
    PDebug::error("D3D11: Failed to create triangle vertex buffer");
    return false;
  }
  D3D11_BUFFER_DESC indices_buffer_info = {};
  indices_buffer_info.ByteWidth = sizeof(UINT) * 64000;  // Max no. of indices
  indices_buffer_info.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
  indices_buffer_info.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
  indices_buffer_info.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device_->CreateBuffer(&indices_buffer_info, nullptr,
                                   &indices_buffer_))) {
    PDebug::error("D3D11: Failed to create index buffer");
    return false;
  }

  // Create cbuffers
  D3D11_BUFFER_DESC cbDesc = {};
  cbDesc.ByteWidth = sizeof(CameraCBuffer);
  cbDesc.Usage = D3D11_USAGE_DYNAMIC;
  cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device_->CreateBuffer(&cbDesc, nullptr, &camera_c_buffer_))) {
    PDebug::error("D3D11: Failed to create camera cbuffer");
    return false;
  }

  cbDesc.ByteWidth = sizeof(VsModelBuffer);
  if (FAILED(device_->CreateBuffer(&cbDesc, nullptr, &vs_model_buffer_))) {
    PDebug::error("D3D11: Failed to create model cbuffer");
    return false;
  }

  D3D11_BLEND_DESC blendDesc = {};
  blendDesc.RenderTarget[0].BlendEnable = TRUE;
  blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(device_->CreateBlendState(&blendDesc, &blendState_))) {
    PDebug::error("D3D11: Failed to create blend state");
    return false;
  }

  return true;
}

void D3D11Application::Render() {
  if (game_scene_) {
    glm::mat4x4 camera_proj_view = {};
    // Set camera buffer
    if (game_scene_->GetCameraProj(camera_proj_view)) {
      // Create camera buffer
      CameraCBuffer camera_c_buffer = {};
      camera_c_buffer.mWorldViewProj =
          DirectX::XMFLOAT4X4{&camera_proj_view[0][0]};
      // map and copy from it
      D3D11_MAPPED_SUBRESOURCE mapped_subresource;
      deviceContext_->Map(camera_c_buffer_.Get(), 0,
                          D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0,
                          &mapped_subresource);
      memcpy(mapped_subresource.pData, &camera_c_buffer, sizeof(CameraCBuffer));
      deviceContext_->Unmap(camera_c_buffer_.Get(), 0);
    }
  }
  D3D11_VIEWPORT viewport = {};
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.Width = static_cast<float>(GetWidth());
  viewport.Height = static_cast<float>(GetHeight());
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  constexpr float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
  constexpr UINT vertexStride = sizeof(Vertex);
  constexpr UINT vertexOffset = 0;

  constexpr UINT indexStride = sizeof(UINT);
  constexpr UINT indexOffset = 0;
  deviceContext_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
  deviceContext_->IASetInputLayout(vertexLayout_.Get());
  deviceContext_->IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deviceContext_->VSSetShader(vertexShader_.Get(), nullptr, 0);

  deviceContext_->RSSetViewports(1, &viewport);
  deviceContext_->PSSetShader(pixelShader_.Get(), nullptr, 0);
  deviceContext_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);

  ID3D11Buffer* constant_buffers[1] = {camera_c_buffer_.Get()};
  deviceContext_->VSSetConstantBuffers(0, 1, constant_buffers);

  float blendFactor[4] = {0, 0, 0, 0};
  deviceContext_->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFF);

  auto primitives = game_scene_->GetPrimitives();
  if (game_scene_) {
    for (const auto& primitive : primitives) {
      {  // Set vertex buffer for object
        D3D11_MAPPED_SUBRESOURCE mapped_subresource;
        deviceContext_->Map(triangleVertices_.Get(), 0,
                            D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_subresource);
        memcpy(mapped_subresource.pData, &primitive.vertices_[0],
               sizeof(Vertex) * primitive.vertices_.size());
        deviceContext_->Unmap(triangleVertices_.Get(), 0);
      }
      {  // Set index buffer
        D3D11_MAPPED_SUBRESOURCE mapped_subresource;
        deviceContext_->Map(indices_buffer_.Get(), 0,
                            D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_subresource);
        memcpy(mapped_subresource.pData, &primitive.indices_[0],
               sizeof(UINT) * primitive.indices_.size());
        deviceContext_->Unmap(indices_buffer_.Get(), 0);
      }
      {  // Update model buffer per object
        VsModelBuffer vs_model_buffer = {};
        vs_model_buffer.scale = PQ_FLOAT3{&primitive.scale_[0]};
        vs_model_buffer.opacity = 1;
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, primitive.world_position_);
        vs_model_buffer.world_position = PQ_MATRIX{&model[0][0]};
        // map and copy from it
        D3D11_MAPPED_SUBRESOURCE mapped_subresource;
        deviceContext_->Map(vs_model_buffer_.Get(), 0,
                            D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_subresource);
        memcpy(mapped_subresource.pData, &vs_model_buffer,
               sizeof(VsModelBuffer));
        deviceContext_->Unmap(vs_model_buffer_.Get(), 0);
      }
      // Configure the buffers created
      deviceContext_->IASetVertexBuffers(0, 1, triangleVertices_.GetAddressOf(),
                                         &vertexStride, &vertexOffset);
      deviceContext_->IASetIndexBuffer(indices_buffer_.Get(),
                                       DXGI_FORMAT_R32_UINT, 0);
      ID3D11Buffer* per_object_cbuffer[1] = {vs_model_buffer_.Get()};
      deviceContext_->VSSetConstantBuffers(1, 1, per_object_cbuffer);
      deviceContext_->DrawIndexed(primitive.indices_.size(), 0, 0);
    }
  }

  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  swapchain_->Present(1, 0);
}

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
