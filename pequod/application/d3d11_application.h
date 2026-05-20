//
// Created by smadhur on 4/5/2026.
//

#ifndef PEQUODENGINE_D3D11_APPLICATION_H
#define PEQUODENGINE_D3D11_APPLICATION_H

#ifdef PEQUOD_GRAPHICS_D3D11

#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl.h>

#include <any>
#include <unordered_map>
#include <vector>

#include "application.h"
#include "globals.h"

namespace Pequod {

struct CameraCBuffer {
  DirectX::XMFLOAT4X4 mWorldViewProj;
  PQ_FLOAT2 mResolution;
  PQ_FLOAT2 _pad;
};

// Cached per-mesh draw-call info, rebuilt only when entities are added or
// removed. instance_count counts how many instance slots in the persistent
// instance_buffer_ belong to this mesh's draw group, starting at
// start_instance.
struct MeshLayoutEntry {
  UINT index_count = 0;
  UINT start_index = 0;
  UINT start_instance = 0;
  UINT instance_count = 0;
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
  void OnNewTick() override;

  void OnResize(int32_t width, int32_t height) override;
  void ImGuiNewFrame() override;
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

  template <typename T>
  bool MapBuffer(ComPtr<ID3D11Buffer>, const std::vector<T>&);
  template <typename T>
  bool MapBuffer(ComPtr<ID3D11Buffer>, const T&);

  // Rebuild mesh_layout_/mesh_order_/entity_slot_ and re-upload the full
  // instance_buffer_ using WRITE_DISCARD. Called on first frame and whenever
  // an EnTT lifecycle signal flips layout_dirty_.
  void RebuildInstanceLayout(entt::registry& registry, TextureAtlas& atlas);

  // Walk every Transform marked dirty, build its VsModelBuffer, and patch
  // just that slot via WRITE_NO_OVERWRITE. No-op if nothing is dirty.
  void UpdateDirtyInstances(entt::registry& registry, TextureAtlas& atlas);

  // Build a VsModelBuffer record for an instance. parent_mesh_entity is the
  // owning Mesh entity (== entity when the entity itself is a Mesh, or the
  // MeshInstance's parent_entity otherwise). Opacity comes from the parent
  // Mesh; atlas UV from entity's Texture2D, else parent's, else white pixel.
  VsModelBuffer BuildInstanceRecord(entt::registry& registry,
                                    TextureAtlas& atlas, entt::entity entity,
                                    entt::entity parent_mesh_entity);

  void EnsureRegistrySignalsHooked(entt::registry& registry);
  void OnLayoutChanged() { layout_dirty_ = true; }

  ComPtr<ID3D11Device> device_ = nullptr;
  ComPtr<ID3D11DeviceContext> deviceContext_ = nullptr;
  ComPtr<IDXGIFactory2> dxgiFactory_ = nullptr;
  ComPtr<IDXGISwapChain1> swapchain_ = nullptr;
  ComPtr<ID3D11RenderTargetView> renderTarget_ = nullptr;
  ComPtr<ID3D11Buffer> triangleVertices_ = nullptr;
  ComPtr<ID3D11Buffer> static_vertices_ =
      nullptr;  // Static geometry like background
  std::vector<Vertex> vertex_buffer_;
  std::vector<UINT> index_buffer_;
  ComPtr<ID3D11Buffer> indices_buffer_ = nullptr;
  ComPtr<ID3D11Buffer> static_indices_buffer_ = nullptr;

  ComPtr<ID3D11Buffer> camera_c_buffer_ = nullptr;
  ComPtr<ID3D11Buffer> instance_buffer_ = nullptr;

  ComPtr<ID3D11InputLayout> vertexLayout_ = nullptr;
  ComPtr<ID3D11InputLayout> static_vertex_layout_ = nullptr;
  ComPtr<ID3D11VertexShader> vertexShader_ = nullptr;
  ComPtr<ID3D11PixelShader> textured_pixel_shader_ = nullptr;
  ComPtr<ID3D11VertexShader> static_vertex_shader_ = nullptr;
  ComPtr<ID3D11BlendState> blendState_ = nullptr;
  ComPtr<ID3D11RasterizerState> rasterizer_state_ = nullptr;
  ComPtr<ID3D11Texture2D> depth_stencil_buffer_ = nullptr;
  ComPtr<ID3D11DepthStencilView> depth_stencil_view_ = nullptr;
  ComPtr<ID3D11DepthStencilState> depth_stencil_state_ = nullptr;
  ComPtr<ID3D11SamplerState> texture_sampler_ = nullptr;

  ComPtr<ID3D11Texture2D> atlas_texture_ = nullptr;
  ComPtr<ID3D11ShaderResourceView> atlas_srv_ = nullptr;

  // Persistent instance layout. Tracked across frames so that frames where
  // nothing structurally changed can patch only the slots whose Transform
  // (or companion Texture2D) is dirty, instead of rebuilding every frame.
  std::unordered_map<entt::entity, MeshLayoutEntry> mesh_layout_;
  std::vector<entt::entity> mesh_order_;
  std::unordered_map<entt::entity, UINT> entity_slot_;
  std::vector<entt::entity> slot_owner_;   // slot index -> entity
  std::vector<entt::entity> slot_parent_;  // slot index -> parent mesh entity
  bool layout_dirty_ = true;
  entt::registry* hooked_registry_ = nullptr;

  static constexpr UINT kMaxInstances = 8192;
};
}  // namespace Pequod

#endif  // PEQUOD_GRAPHICS_D3D11

#endif  // PEQUODENGINE_D3D11_APPLICATION_H
