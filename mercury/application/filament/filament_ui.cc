// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "filament_ui.h"

#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <utils/EntityManager.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "ui/ui_texture.h"

namespace Mercury::fil {

namespace {

// The interleaved vertex the UI material reads: position in device (NDC)
// coordinates, UVs, sRGB color, the UI draw mode parameters and the text
// outline color.
struct UiVertex {
  float position[3];
  float uv[2];
  uint8_t color[4];
  float params[4];
  uint8_t outline[4];
};
static_assert(sizeof(UiVertex) == 44);

// Uploads hand ownership of their buffer to Filament, which releases it
// through this callback once the copy has been consumed.
void DeleteUpload(void* buffer, size_t /*size*/, void* /*user*/) {
  delete[] static_cast<uint8_t*>(buffer);
}

uint8_t* CopyToHeap(const void* data, size_t bytes) {
  auto* copy = new uint8_t[bytes];
  std::memcpy(copy, data, bytes);
  return copy;
}

}  // namespace

bool FilamentUiRenderer::Init(filament::Engine* engine) {
  scene_ = engine->createScene();
  const fs::path path = os::EngineMaterialDir() / "ui.filamat";
  const auto bytes = os::ReadBytes(path);
  if (!bytes) {
    PDebug::error("Filament: could not open material {}", path.string());
    return false;
  }
  material_ = filament::Material::Builder()
                  .package(bytes->data(), bytes->size())
                  .build(*engine);
  return scene_ && material_;
}

void FilamentUiRenderer::Shutdown(filament::Engine* engine) {
  if (!engine) return;
  for (const RenderableRecord& record : renderables_) {
    if (scene_) scene_->remove(record.renderable);
    engine->destroy(record.renderable);
    if (record.material) engine->destroy(record.material);
  }
  renderables_.clear();
  for (const GarbageMaterial& garbage : garbage_materials_) {
    engine->destroy(garbage.material);
  }
  garbage_materials_.clear();
  if (vertices_) {
    engine->destroy(vertices_);
    vertices_ = nullptr;
  }
  if (indices_) {
    engine->destroy(indices_);
    indices_ = nullptr;
  }
  vertex_capacity_ = 0;
  index_capacity_ = 0;
  for (auto& [id, entry] : textures_) {
    engine->destroy(entry.texture);
  }
  textures_.clear();
  for (const GarbageTexture& garbage : garbage_) {
    engine->destroy(garbage.texture);
  }
  garbage_.clear();
  if (white_) {
    engine->destroy(white_);
    white_ = nullptr;
  }
  if (material_) {
    engine->destroy(material_);
    material_ = nullptr;
  }
  if (scene_) {
    engine->destroy(scene_);
    scene_ = nullptr;
  }
}

filament::Texture* FilamentUiRenderer::SyncTexture(filament::Engine* engine,
                                                   uint32_t id) {
  // Command 0 means "no texture": a 1x1 white pixel so the sampler is
  // always bound (Filament panics on unbound samplers).
  if (id == 0) {
    if (!white_) {
      white_ = filament::Texture::Builder()
                  .width(1)
                  .height(1)
                  .levels(1)
                  .usage(filament::Texture::Usage::SAMPLEABLE |
                         filament::Texture::Usage::UPLOADABLE)
                  .format(filament::Texture::InternalFormat::SRGB8_A8)
                  .sampler(filament::Texture::Sampler::SAMPLER_2D)
                  .build(*engine);
      if (white_) {
        const uint8_t pixel[4] = {0xff, 0xff, 0xff, 0xff};
        white_->setImage(
            *engine, 0,
            filament::Texture::PixelBufferDescriptor(
                CopyToHeap(pixel, sizeof(pixel)), sizeof(pixel),
                filament::backend::PixelDataFormat::RGBA,
                filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
                DeleteUpload));
      }
    }
    return white_;
  }
  auto* registry = &UI::UITextureRegistry::Instance();
  std::shared_ptr<UI::UITexture> texture = registry->Find(id);
  if (!texture) {
    return white_;
  }
  if (texture->width <= 0 || texture->height <= 0 || texture->rgba.empty()) {
    return white_;
  }

  auto it = textures_.find(id);
  // The glyph atlas is rebuilt while text is laid out, so the same texture
  // is re-uploaded rather than recreated: a fresh Filament texture would be
  // sampled before its contents are usable (and destroying the old one races
  // with frames in flight).
  if (it != textures_.end() &&
      it->second.width == texture->width &&
      it->second.height == texture->height) {
    if (it->second.version != texture->version) {
      it->second.texture->setImage(
          *engine, 0,
          filament::Texture::PixelBufferDescriptor(
              CopyToHeap(texture->rgba.data(), texture->rgba.size()),
              texture->rgba.size(), filament::backend::PixelDataFormat::RGBA,
              filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
              DeleteUpload));
      it->second.version = texture->version;
    }
    return it->second.texture;
  }
  if (it != textures_.end()) {
    garbage_.push_back({frame_, it->second.texture});
    textures_.erase(it);
  }

  filament::Texture* gpu =
      filament::Texture::Builder()
          .width(static_cast<uint32_t>(texture->width))
          .height(static_cast<uint32_t>(texture->height))
          .levels(1)
          .usage(filament::Texture::Usage::SAMPLEABLE |
                 filament::Texture::Usage::UPLOADABLE)
          .format(filament::Texture::InternalFormat::RGBA8)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);
  if (!gpu) return nullptr;
  gpu->setImage(*engine, 0,
                filament::Texture::PixelBufferDescriptor(
                    CopyToHeap(texture->rgba.data(), texture->rgba.size()),
                    texture->rgba.size(),
                    filament::backend::PixelDataFormat::RGBA,
                    filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
                    DeleteUpload));
  textures_.emplace(id, TextureEntry{gpu, texture->version,
                                     texture->width, texture->height});
  return gpu;
}

void FilamentUiRenderer::DestroyRenderables(filament::Engine* engine) {
  for (const RenderableRecord& record : renderables_) {
    if (scene_) scene_->remove(record.renderable);
    engine->destroy(record.renderable);
    if (record.material) garbage_materials_.push_back({frame_, record.material});
  }
  renderables_.clear();
}

void FilamentUiRenderer::Prepare(filament::Engine* engine,
                                 const UI::UIDrawData& data) {
  frame_++;
  DestroyRenderables(engine);
  // Resources replaced this frame may still be referenced by a frame in
  // flight; three frames later it is safe to free.
  std::erase_if(garbage_, [&](const GarbageTexture& garbage) {
    if (garbage.frame + 3 > frame_) return false;
    engine->destroy(garbage.texture);
    return true;
  });
  std::erase_if(garbage_materials_, [&](const GarbageMaterial& garbage) {
    if (garbage.frame + 3 > frame_) return false;
    engine->destroy(garbage.material);
    return true;
  });

  if (data.commands.empty() || data.target_size.x <= 0 ||
      data.target_size.y <= 0) {
    return;
  }

  const uint32_t vertex_count = static_cast<uint32_t>(data.vertices.size());
  const uint32_t index_count = static_cast<uint32_t>(data.indices.size());
  if (vertex_count > vertex_capacity_ || index_count > index_capacity_) {
    engine->destroy(vertices_);
    engine->destroy(indices_);
    vertex_capacity_ = std::max(vertex_count, vertex_capacity_ * 2);
    index_capacity_ = std::max(index_count, index_capacity_ * 2);
    vertices_ = filament::VertexBuffer::Builder()
                    .vertexCount(vertex_capacity_)
                    .bufferCount(1)
                    .attribute(filament::VertexAttribute::POSITION, 0,
                               filament::VertexBuffer::AttributeType::FLOAT3,
                               offsetof(UiVertex, position), sizeof(UiVertex))
                    .attribute(filament::VertexAttribute::UV0, 0,
                               filament::VertexBuffer::AttributeType::FLOAT2,
                               offsetof(UiVertex, uv), sizeof(UiVertex))
                    .attribute(filament::VertexAttribute::COLOR, 0,
                               filament::VertexBuffer::AttributeType::UBYTE4,
                               offsetof(UiVertex, color), sizeof(UiVertex))
                    .normalized(filament::VertexAttribute::COLOR)
                    .attribute(filament::VertexAttribute::CUSTOM0, 0,
                               filament::VertexBuffer::AttributeType::FLOAT4,
                               offsetof(UiVertex, params), sizeof(UiVertex))
                    .attribute(filament::VertexAttribute::CUSTOM1, 0,
                               filament::VertexBuffer::AttributeType::UBYTE4,
                               offsetof(UiVertex, outline), sizeof(UiVertex))
                    .normalized(filament::VertexAttribute::CUSTOM1)
                    .build(*engine);
    indices_ = filament::IndexBuffer::Builder()
                  .indexCount(index_capacity_)
                  .bufferType(filament::IndexBuffer::IndexType::UINT)
                  .build(*engine);
    if (!vertices_ || !indices_) return;
  }

  // Pixels (y down) to device coordinates (y up).
  std::vector<UiVertex> packed(data.vertices.size());
  const float inv_w = 2.0f / static_cast<float>(data.target_size.x);
  const float inv_h = 2.0f / static_cast<float>(data.target_size.y);
  for (size_t i = 0; i < data.vertices.size(); i++) {
    const UI::UIVertex& vertex = data.vertices[i];
    UiVertex& out = packed[i];
    out.position[0] = vertex.position.x * inv_w - 1.0f;
    out.position[1] = 1.0f - vertex.position.y * inv_h;
    out.position[2] = 0.0f;
    out.uv[0] = vertex.uv.x;
    out.uv[1] = vertex.uv.y;
    std::memcpy(out.color, &vertex.color, 4);
    out.params[0] = vertex.params.x;
    out.params[1] = vertex.params.y;
    out.params[2] = vertex.params.z;
    out.params[3] = vertex.params.w;
    std::memcpy(out.outline, &vertex.outline_color, 4);
  }
  const size_t vertex_bytes = packed.size() * sizeof(UiVertex);
  const size_t index_bytes = data.indices.size() * sizeof(uint32_t);
  vertices_->setBufferAt(*engine, 0,
                         {CopyToHeap(packed.data(), vertex_bytes),
                          vertex_bytes, DeleteUpload});
  indices_->setBuffer(*engine,
                      {CopyToHeap(data.indices.data(), index_bytes),
                       index_bytes, DeleteUpload});

  for (size_t cmd_index = 0; cmd_index < data.commands.size(); cmd_index++) {
    const UI::UIDrawCommand& command = data.commands[cmd_index];
    if (command.index_count == 0) continue;
    filament::MaterialInstance* instance = material_->createInstance();
    filament::Texture* texture = SyncTexture(engine, command.texture_id);
    const filament::TextureSampler sampler(
        filament::TextureSampler::MinFilter::LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    instance->setParameter("uiTexture", texture, sampler);

    utils::Entity renderable = utils::EntityManager::get().create();
    filament::RenderableManager::Builder build(1);
    build.boundingBox(filament::Box(filament::math::float3(-1.0f),
                                    filament::math::float3(1.0f)))
         .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                   vertices_, indices_, command.index_offset,
                   command.index_count)
         .material(0, instance)
         .blendOrder(0, static_cast<uint16_t>(std::min<size_t>(cmd_index, 0x7fffu)))
         .globalBlendOrderEnabled(0, true)
         .culling(false);
    if (build.build(*engine, renderable) !=
        filament::RenderableManager::Builder::Success) {
      engine->destroy(renderable);
      engine->destroy(instance);
      continue;
    }
    scene_->addEntity(renderable);
    renderables_.push_back({renderable, instance});
  }
}

}  // namespace Mercury::fil
