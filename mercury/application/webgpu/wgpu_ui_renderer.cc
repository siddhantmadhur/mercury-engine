#include "wgpu_ui_renderer.h"

#include <algorithm>

#include "debugger/debugger.h"
#include "ui/ui_texture.h"

namespace Mercury::wgpu_ui {

namespace {

// Mirrors shaders/webgpu/ui.wgsl.
constexpr const char* kUIShaderWGSL = R"(
struct UIUniforms {
    inv_target_size: vec2<f32>,
    padding: vec2<f32>,
};

@group(0) @binding(0) var<uniform> ui: UIUniforms;
@group(1) @binding(0) var ui_texture: texture_2d<f32>;
@group(1) @binding(1) var ui_sampler: sampler;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) color: vec4<f32>,
    @location(3) outline: vec4<f32>,
    @location(4) params: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) outline: vec4<f32>,
    // x: mode (0 solid, 1 textured, 2 SDF text), y: SDF edge,
    // z: outline width in SDF units, w: extra softness in SDF units
    @location(3) @interpolate(flat) params: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let ndc = input.position * ui.inv_target_size * 2.0 - 1.0;
    // WebGPU's clip space has +y up.
    out.position = vec4<f32>(ndc.x, -ndc.y, 0.0, 1.0);
    out.uv = input.uv;
    out.color = input.color;
    out.outline = input.outline;
    out.params = input.params;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sampled before branching: derivatives need uniform control flow.
    let texel = textureSample(ui_texture, ui_sampler, input.uv);
    let width = max(fwidth(texel.a) * 0.7, 1e-4) + input.params.w;
    let mode = u32(input.params.x + 0.5);
    if (mode == 0u) {
        return input.color;
    }
    if (mode == 1u) {
        return texel * input.color;
    }
    let edge = input.params.y;
    let fill = smoothstep(edge - width, edge + width, texel.a);
    if (input.params.z <= 0.0) {
        return vec4<f32>(input.color.rgb, input.color.a * fill);
    }
    let outer_edge = edge - input.params.z;
    let coverage = smoothstep(outer_edge - width, outer_edge + width, texel.a);
    let color = mix(input.outline, input.color, fill);
    return vec4<f32>(color.rgb, color.a * coverage);
}
)";

}  // namespace

bool UIRenderer::Init(wgpu::Device device, wgpu::Queue queue,
                      wgpu::TextureFormat color_format,
                      wgpu::TextureFormat depth_format) {
  device_ = device;
  queue_ = queue;

  wgpu::ShaderSourceWGSL wgsl = {};
  wgsl.code = kUIShaderWGSL;
  wgpu::ShaderModuleDescriptor module_desc = {};
  module_desc.nextInChain = &wgsl;
  wgpu::ShaderModule module = device_.CreateShaderModule(&module_desc);

  wgpu::BindGroupLayoutEntry uniform_entry = {};
  uniform_entry.binding = 0;
  uniform_entry.visibility = wgpu::ShaderStage::Vertex;
  uniform_entry.buffer.type = wgpu::BufferBindingType::Uniform;
  uniform_entry.buffer.minBindingSize = 16;
  wgpu::BindGroupLayoutDescriptor uniform_layout_desc = {};
  uniform_layout_desc.entryCount = 1;
  uniform_layout_desc.entries = &uniform_entry;
  uniform_layout_ = device_.CreateBindGroupLayout(&uniform_layout_desc);

  wgpu::BindGroupLayoutEntry texture_entries[2] = {};
  texture_entries[0].binding = 0;
  texture_entries[0].visibility = wgpu::ShaderStage::Fragment;
  texture_entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
  texture_entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  texture_entries[1].binding = 1;
  texture_entries[1].visibility = wgpu::ShaderStage::Fragment;
  texture_entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
  wgpu::BindGroupLayoutDescriptor texture_layout_desc = {};
  texture_layout_desc.entryCount = 2;
  texture_layout_desc.entries = texture_entries;
  texture_layout_ = device_.CreateBindGroupLayout(&texture_layout_desc);

  wgpu::BindGroupLayout layouts[2] = {uniform_layout_, texture_layout_};
  wgpu::PipelineLayoutDescriptor pipeline_layout_desc = {};
  pipeline_layout_desc.bindGroupLayoutCount = 2;
  pipeline_layout_desc.bindGroupLayouts = layouts;
  wgpu::PipelineLayout pipeline_layout = device_.CreatePipelineLayout(&pipeline_layout_desc);

  wgpu::BufferDescriptor uniform_desc = {};
  uniform_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  uniform_desc.size = 16;
  uniform_buffer_ = device_.CreateBuffer(&uniform_desc);
  wgpu::BindGroupEntry uniform_bind = {};
  uniform_bind.binding = 0;
  uniform_bind.buffer = uniform_buffer_;
  uniform_bind.size = 16;
  wgpu::BindGroupDescriptor uniform_group_desc = {};
  uniform_group_desc.layout = uniform_layout_;
  uniform_group_desc.entryCount = 1;
  uniform_group_desc.entries = &uniform_bind;
  uniform_group_ = device_.CreateBindGroup(&uniform_group_desc);

  wgpu::SamplerDescriptor sampler_desc = {};
  sampler_desc.magFilter = wgpu::FilterMode::Linear;
  sampler_desc.minFilter = wgpu::FilterMode::Linear;
  linear_sampler_ = device_.CreateSampler(&sampler_desc);
  sampler_desc.magFilter = wgpu::FilterMode::Nearest;
  sampler_desc.minFilter = wgpu::FilterMode::Nearest;
  nearest_sampler_ = device_.CreateSampler(&sampler_desc);

  wgpu::VertexAttribute attributes[5] = {};
  attributes[0] = {nullptr, wgpu::VertexFormat::Float32x2, offsetof(UI::UIVertex, position), 0};
  attributes[1] = {nullptr, wgpu::VertexFormat::Float32x2, offsetof(UI::UIVertex, uv), 1};
  attributes[2] = {nullptr, wgpu::VertexFormat::Unorm8x4, offsetof(UI::UIVertex, color), 2};
  attributes[3] = {nullptr, wgpu::VertexFormat::Unorm8x4,
                   offsetof(UI::UIVertex, outline_color), 3};
  attributes[4] = {nullptr, wgpu::VertexFormat::Float32x4, offsetof(UI::UIVertex, params), 4};
  wgpu::VertexBufferLayout vertex_layout = {};
  vertex_layout.arrayStride = sizeof(UI::UIVertex);
  vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
  vertex_layout.attributeCount = 5;
  vertex_layout.attributes = attributes;

  wgpu::BlendState blend = {};
  blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
  blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  blend.color.operation = wgpu::BlendOperation::Add;
  blend.alpha.srcFactor = wgpu::BlendFactor::One;
  blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  blend.alpha.operation = wgpu::BlendOperation::Add;
  wgpu::ColorTargetState target = {};
  target.format = color_format;
  target.blend = &blend;
  wgpu::FragmentState fragment = {};
  fragment.module = module;
  fragment.entryPoint = "fs_main";
  fragment.targetCount = 1;
  fragment.targets = &target;

  // Drawn over everything; no depth test or write.
  wgpu::DepthStencilState depth = {};
  depth.format = depth_format;
  depth.depthWriteEnabled = false;
  depth.depthCompare = wgpu::CompareFunction::Always;

  wgpu::RenderPipelineDescriptor pipeline_desc = {};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.vertex.bufferCount = 1;
  pipeline_desc.vertex.buffers = &vertex_layout;
  pipeline_desc.fragment = &fragment;
  pipeline_desc.depthStencil = &depth;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
  pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
  return pipeline_ != nullptr;
}

bool UIRenderer::EnsureBuffer(wgpu::Buffer& buffer, uint64_t& capacity, uint64_t bytes,
                              wgpu::BufferUsage usage) {
  if (buffer && capacity >= bytes) return true;
  uint64_t size = std::max<uint64_t>(capacity, 64 * 1024);
  while (size < bytes) size *= 2;
  wgpu::BufferDescriptor desc = {};
  desc.usage = usage | wgpu::BufferUsage::CopyDst;
  desc.size = size;
  buffer = device_.CreateBuffer(&desc);
  capacity = size;
  return buffer != nullptr;
}

UIRenderer::GpuTexture* UIRenderer::SyncTexture(uint32_t id) {
  static const uint8_t kWhite[4] = {255, 255, 255, 255};
  std::shared_ptr<UI::UITexture> cpu;
  const uint8_t* pixels = kWhite;
  int width = 1, height = 1;
  uint64_t version = 1;
  bool linear = true;
  if (id != 0) {
    cpu = UI::UITextureRegistry::Instance().Find(id);
    if (!cpu || cpu->width <= 0 || cpu->height <= 0 || cpu->rgba.empty()) {
      return SyncTexture(0);
    }
    pixels = cpu->rgba.data();
    width = cpu->width;
    height = cpu->height;
    version = cpu->version;
    linear = cpu->linear_filter;
  }
  GpuTexture& texture = textures_[id];
  if (texture.texture && texture.version == version) return &texture;
  if (!texture.texture || texture.width != width || texture.height != height ||
      texture.linear != linear) {
    wgpu::TextureDescriptor desc = {};
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    texture.texture = device_.CreateTexture(&desc);
    wgpu::BindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].textureView = texture.texture.CreateView();
    entries[1].binding = 1;
    entries[1].sampler = linear ? linear_sampler_ : nearest_sampler_;
    wgpu::BindGroupDescriptor group_desc = {};
    group_desc.layout = texture_layout_;
    group_desc.entryCount = 2;
    group_desc.entries = entries;
    texture.bind_group = device_.CreateBindGroup(&group_desc);
    texture.width = width;
    texture.height = height;
    texture.linear = linear;
  }
  wgpu::TexelCopyTextureInfo dst = {};
  dst.texture = texture.texture;
  wgpu::TexelCopyBufferLayout layout = {};
  layout.bytesPerRow = static_cast<uint32_t>(width) * 4;
  layout.rowsPerImage = static_cast<uint32_t>(height);
  wgpu::Extent3D extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  queue_.WriteTexture(&dst, pixels, size_t(width) * height * 4, &layout, &extent);
  texture.version = version;
  return &texture;
}

bool UIRenderer::Prepare(const UI::UIDrawData& data, uint32_t width, uint32_t height) {
  prepared_ = nullptr;
  if (!pipeline_ || data.empty() || width == 0 || height == 0) return false;

  // Drop GPU copies of textures the UI released.
  auto& registry = UI::UITextureRegistry::Instance();
  if (registry.generation() != registry_generation_) {
    registry_generation_ = registry.generation();
    std::erase_if(textures_, [&](const auto& entry) {
      return entry.first != 0 && !registry.Find(entry.first);
    });
  }
  for (const auto& command : data.commands) SyncTexture(command.texture_id);
  SyncTexture(0);

  const uint64_t vertex_bytes = data.vertices.size() * sizeof(UI::UIVertex);
  const uint64_t index_bytes = data.indices.size() * sizeof(uint32_t);
  if (!EnsureBuffer(vertex_buffer_, vertex_capacity_, vertex_bytes,
                    wgpu::BufferUsage::Vertex) ||
      !EnsureBuffer(index_buffer_, index_capacity_, index_bytes, wgpu::BufferUsage::Index)) {
    return false;
  }
  queue_.WriteBuffer(vertex_buffer_, 0, data.vertices.data(), vertex_bytes);
  queue_.WriteBuffer(index_buffer_, 0, data.indices.data(), index_bytes);
  const float uniforms[4] = {1.0f / width, 1.0f / height, 0.0f, 0.0f};
  queue_.WriteBuffer(uniform_buffer_, 0, uniforms, sizeof(uniforms));
  prepared_ = &data;
  return true;
}

void UIRenderer::Record(wgpu::RenderPassEncoder& pass, uint32_t width, uint32_t height) {
  if (!prepared_) return;
  const UI::UIDrawData& data = *prepared_;
  pass.SetPipeline(pipeline_);
  pass.SetBindGroup(0, uniform_group_);
  pass.SetVertexBuffer(0, vertex_buffer_, 0, data.vertices.size() * sizeof(UI::UIVertex));
  pass.SetIndexBuffer(index_buffer_, wgpu::IndexFormat::Uint32, 0,
                      data.indices.size() * sizeof(uint32_t));
  for (const auto& command : data.commands) {
    if (command.index_count == 0) continue;
    auto it = textures_.find(command.texture_id);
    if (it == textures_.end()) it = textures_.find(0);
    if (it == textures_.end()) continue;
    int x = 0, y = 0, w = static_cast<int>(width), h = static_cast<int>(height);
    if (command.clip.z >= 0) {
      x = std::clamp(command.clip.x, 0, w);
      y = std::clamp(command.clip.y, 0, h);
      w = std::clamp(command.clip.x + command.clip.z, 0, w) - x;
      h = std::clamp(command.clip.y + command.clip.w, 0, h) - y;
    }
    if (w <= 0 || h <= 0) continue;
    pass.SetBindGroup(1, it->second.bind_group);
    pass.SetScissorRect(x, y, w, h);
    pass.DrawIndexed(command.index_count, 1, command.index_offset, 0, 0);
  }
  pass.SetScissorRect(0, 0, width, height);
  prepared_ = nullptr;
}

}  // namespace Mercury::wgpu_ui
