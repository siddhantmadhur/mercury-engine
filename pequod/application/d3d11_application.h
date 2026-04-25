//
// Created by smadhur on 4/5/2026.
//

#ifndef PEQUODENGINE_D3D11_APPLICATION_H
#define PEQUODENGINE_D3D11_APPLICATION_H

#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl.h>

#include <unordered_map>

#include "application.h"

namespace Pequod {

struct CameraCBuffer {
  DirectX::XMFLOAT4X4 mWorldViewProj;
};

class D3D11Application : public Application {
  template <typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

 public:
  D3D11Application(const std::string& window_title, float initial_width = 1280,
                   float initial_height = 720);

 protected:
  bool OnLoad() override;      // Runs when the application is created
  void Render() override;      // Renders objects
  bool Initialize() override;  // Renders objects

  void OnResize(int32_t width, int32_t height) override;
  static bool CompileShader(const std::wstring& fileName,
                            const std::string& entryPoint,
                            const std::string& profile,
                            ComPtr<ID3DBlob>& shaderBlob);

  [[nodiscard]] ComPtr<ID3D11VertexShader> CreateVertexShader(
      const std::wstring& fileName, ComPtr<ID3DBlob>& vertexShaderBlob) const;

  [[nodiscard]] ComPtr<ID3D11PixelShader> CreatePixelShader(
      const std::wstring& fileName) const;

 private:
  bool CreateSwapchainResources();
  void DestroySwapchainResources();

  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> deviceContext_ = nullptr;
  ComPtr<IDXGIFactory2> dxgiFactory_ = nullptr;
  ComPtr<IDXGISwapChain1> swapchain_ = nullptr;
  ComPtr<ID3D11RenderTargetView> renderTarget_ = nullptr;
  ComPtr<ID3D11Buffer> triangleVertices_ = nullptr;
  ComPtr<ID3D11Buffer> indices_buffer_ = nullptr;

  ComPtr<ID3D11Buffer> camera_c_buffer_ = nullptr;
  ComPtr<ID3D11Buffer> vs_model_buffer_ = nullptr;

  ComPtr<ID3D11InputLayout> vertexLayout_ = nullptr;
  ComPtr<ID3D11VertexShader> vertexShader_ = nullptr;
  ComPtr<ID3D11PixelShader> pixelShader_ = nullptr;
  ComPtr<ID3D11PixelShader> textured_pixel_shader_ = nullptr;
  ComPtr<ID3D11BlendState> blendState_ = nullptr;
  ComPtr<ID3D11SamplerState> texture_sampler_ = nullptr;

  std::unordered_map<const unsigned char*,
                     ComPtr<ID3D11ShaderResourceView>> texture_cache_;

  ID3D11ShaderResourceView* GetOrCreateSRV(const unsigned char* data,
                                           int width, int height);

  std::vector<Primitive> primitives_ = {};
};
}  // namespace Pequod

#endif  // PEQUODENGINE_D3D11_APPLICATION_H
