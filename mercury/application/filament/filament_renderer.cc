// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "filament_renderer.h"

#include <filament/Color.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <geometry/SurfaceOrientation.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "components/components.h"
#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "os/filesystem.h"
#include "properties/transform.h"
#include "scene/map_scene.h"

namespace Mercury::fil {

namespace {

// Filament lights use physical units; Mercury's intensities are Unity-like
// multipliers around 1. These keep the two talking.
constexpr float kSunLuxPerIntensity = 100000.0f;
constexpr float kPointLumensPerIntensity = 1200.0f;
// How often expired mesh/texture sources are swept away.
constexpr uint64_t kSweepEvery = 30;

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

float SrgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 SrgbToLinear(const glm::vec3& c) {
  return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)};
}

// Filament's matrices are column-major like glm's, so the scalar
// constructor takes glm's column order directly.
filament::math::float2 ToFilament(const glm::vec2& v) {
  return {v.x, v.y};
}
filament::math::float3 ToFilament(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}
filament::math::float4 ToFilament(const glm::vec4& v) {
  return {v.x, v.y, v.z, v.w};
}
filament::math::mat4f ToFilament(const glm::mat4& m) {
  return filament::math::mat4f(
      m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3],
      m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3]);
}

// One byte per channel, normalized, which is what Filament's COLOR
// attribute takes.
std::vector<uint8_t> PackColors(const MeshGeometry& geometry) {
  std::vector<uint8_t> colors(geometry.vertices.size() * 4);
  for (size_t i = 0; i < geometry.vertices.size(); i++) {
    const glm::vec4 c = glm::clamp(geometry.vertices[i].color, 0.0f, 1.0f);
    colors[i * 4 + 0] = static_cast<uint8_t>(c.r * 255.0f + 0.5f);
    colors[i * 4 + 1] = static_cast<uint8_t>(c.g * 255.0f + 0.5f);
    colors[i * 4 + 2] = static_cast<uint8_t>(c.b * 255.0f + 0.5f);
    colors[i * 4 + 3] = static_cast<uint8_t>(c.a * 255.0f + 0.5f);
  }
  return colors;
}

// Tangent frames as quaternions, Filament's TANGENTS attribute. The
// builder takes tangents exactly as ModelVertex stores them: xyz direction,
// w the bitangent's sign.
std::vector<filament::math::quatf> PackTangents(
    const MeshGeometry& source) {
  const size_t count = source.vertices.size();
  std::vector<filament::math::float3> normals(count);
  std::vector<filament::math::float4> tangents(count);
  for (size_t i = 0; i < count; i++) {
    const ModelVertex& v = source.vertices[i];
    normals[i] = ToFilament(v.normal);
    tangents[i] = ToFilament(v.tangent);
  }
  filament::geometry::SurfaceOrientation::Builder builder;
  builder.vertexCount(count);
  builder.normals(normals.data(), sizeof(filament::math::float3));
  builder.tangents(tangents.data(), sizeof(filament::math::float4));
  std::unique_ptr<filament::geometry::SurfaceOrientation> orientation(builder.build());
  std::vector<filament::math::quatf> quats(count);
  if (orientation) {
    orientation->getQuats(quats.data(), count,
                          sizeof(filament::math::quatf));
  } else {
    // No usable tangent data: identity frames (the normal still reads).
    for (size_t i = 0; i < count; i++) quats[i] = {1.0f, 0.0f, 0.0f, 0.0f};
  }
  return quats;
}

}  // namespace

bool FilamentRenderer::Init(filament::Engine* engine) {
  engine_ = engine;
  lit_[0][0] = LoadMaterial("lit_opaque");
  lit_[0][1] = LoadMaterial("lit_opaque_double_sided");
  lit_[1][0] = LoadMaterial("lit_transparent");
  lit_[1][1] = LoadMaterial("lit_transparent_double_sided");
  lit_[2][0] = LoadMaterial("lit_transmissive");
  lit_[2][1] = LoadMaterial("lit_transmissive_double_sided");
  unlit_ = LoadMaterial("unlit");
  sdf_ = LoadMaterial("text_sdf");
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      if (!lit_[i][j]) return false;
    }
  }
  if (!unlit_ || !sdf_) return false;
  scene_ = engine_->createScene();

  // The texture shown when a material has no image: 1x1 white, plus a 1x1
  // flat tangent-space normal. Filament panics at draw time on unbound
  // samplers, so every material sampler is always bound to something.
  auto make_1x1 = [&](filament::Texture::InternalFormat format,
                      const uint8_t pixel[4]) {
    filament::Texture* tex = filament::Texture::Builder()
                                 .width(1)
                                 .height(1)
                                 .levels(1)
                                 .usage(filament::Texture::Usage::SAMPLEABLE |
                                        filament::Texture::Usage::UPLOADABLE)
                                 .format(format)
                                 .sampler(filament::Texture::Sampler::SAMPLER_2D)
                                 .build(*engine_);
    if (tex) {
      tex->setImage(
          *engine_, 0,
          filament::Texture::PixelBufferDescriptor(
              CopyToHeap(pixel, 4), 4, filament::backend::PixelDataFormat::RGBA,
              filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0,
              DeleteUpload));
    }
    return tex;
  };
  const uint8_t white_px[4] = {0xff, 0xff, 0xff, 0xff};
  const uint8_t normal_px[4] = {0x80, 0x80, 0xff, 0xff};
  white_ = make_1x1(filament::Texture::InternalFormat::SRGB8_A8, white_px);
  flat_normal_ = make_1x1(filament::Texture::InternalFormat::RGBA8, normal_px);
  return scene_ != nullptr && white_ != nullptr && flat_normal_ != nullptr;
}

filament::Material* FilamentRenderer::LoadMaterial(const char* name) {
  const fs::path path =
      os::EngineMaterialDir() / (std::string(name) + ".filamat");
  const auto bytes = os::ReadBytes(path);
  if (!bytes) {
    PDebug::error("Filament: could not open material {}", path.string());
    return nullptr;
  }
  filament::Material* material = filament::Material::Builder()
                                    .package(bytes->data(), bytes->size())
                                    .build(*engine_);
  if (!material) {
    PDebug::error("Filament: could not compile material {}", name);
  }
  return material;
}

void FilamentRenderer::SetFallbackSkybox(filament::Skybox* skybox) {
  fallback_sky_ = skybox;
  if (scene_ && !has_panorama_ibl_) {
    scene_->setSkybox(fallback_sky_);
  }
}

void FilamentRenderer::SweepGarbage(filament::Engine* engine, bool all) {
  auto ready = [&](const auto& item) {
    return all || item.first + 3 <= frame_;
  };
  // Destroy Skybox and IndirectLight before the Texture objects they sample.
  std::erase_if(garbage_skyboxes_, [&](const auto& item) {
    if (!ready(item)) return false;
    engine->destroy(item.second);
    return true;
  });
  std::erase_if(garbage_indirect_lights_, [&](const auto& item) {
    if (!ready(item)) return false;
    engine->destroy(item.second);
    return true;
  });
  std::erase_if(garbage_textures_, [&](const auto& item) {
    if (!ready(item)) return false;
    engine->destroy(item.second);
    return true;
  });
}

void FilamentRenderer::Shutdown(filament::Engine* engine) {
  if (!engine) return;
  for (auto& [entity, record] : proxies_) {
    for (DrawRecord& draw : record.draws) {
      if (scene_ && draw.in_scene) scene_->remove(draw.renderable);
      engine->destroy(draw.renderable);
      engine->destroy(draw.material);
    }
  }
  proxies_.clear();
  DestroyLights(engine);
  SweepGarbage(engine, true);
  fallback_sky_ = nullptr;
  for (auto& [id, entry] : meshes_) {
    engine->destroy(entry.vertices);
    engine->destroy(entry.indices);
  }
  meshes_.clear();
  for (auto& [id, entry] : textures_) {
    engine->destroy(entry.texture);
  }
  textures_.clear();
  if (white_) {
    engine->destroy(white_);
    white_ = nullptr;
  }
  if (flat_normal_) {
    engine->destroy(flat_normal_);
    flat_normal_ = nullptr;
  }
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      if (lit_[i][j]) {
        engine->destroy(lit_[i][j]);
        lit_[i][j] = nullptr;
      }
    }
  }
  if (unlit_) {
    engine->destroy(unlit_);
    unlit_ = nullptr;
  }
  if (sdf_) {
    engine->destroy(sdf_);
    sdf_ = nullptr;
  }
  if (scene_) {
    engine->destroy(scene_);
    scene_ = nullptr;
  }
  sun_direction_ = glm::vec3(0.0f);
  sun_color_ = glm::vec3(0.0f);
  sun_shadows_ = false;
  engine_ = nullptr;
}

filament::Texture* FilamentRenderer::UploadTexture(
    filament::Engine* engine, const ImageData& image) {
  if (auto it = textures_.find(image.id); it != textures_.end())
    return it->second.texture;
  if (image.pixels.empty()) return nullptr;

  filament::Texture* texture =
      filament::Texture::Builder()
          .width(static_cast<uint32_t>(image.width))
          .height(static_cast<uint32_t>(image.height))
          .levels(0xFF)
          .usage(filament::Texture::Usage::SAMPLEABLE |
                 filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::GEN_MIPMAPPABLE)
          .format(image.srgb ? filament::Texture::InternalFormat::SRGB8_A8
                             : filament::Texture::InternalFormat::RGBA8)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);
  if (!texture) return nullptr;
  const size_t bytes = image.pixels.size();
  texture->setImage(
      *engine, 0,
      filament::Texture::PixelBufferDescriptor(
          CopyToHeap(image.pixels.data(), bytes), bytes,
          filament::backend::PixelDataFormat::RGBA,
          filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0, DeleteUpload));
  texture->generateMipmaps(*engine);

  TextureEntry entry;
  entry.texture = texture;
  textures_.emplace(image.id, entry);
  return texture;
}

filament::VertexBuffer* FilamentRenderer::UploadGeometry(
    filament::Engine* engine, const MeshGeometry& geometry,
    filament::Box* bounds) {
  const size_t count = geometry.vertices.size();
  if (count == 0 || geometry.indices.empty()) return nullptr;

  std::vector<uint8_t> colors = PackColors(geometry);
  std::vector<filament::math::quatf> quats = PackTangents(geometry);
  std::vector<glm::vec2> uvs(count);
  for (size_t i = 0; i < count; i++) uvs[i] = geometry.vertices[i].uv;
  // Filament takes bone indices as ushort4; Mercury's are uint32.
  std::vector<uint16_t> joints;
  if (geometry.skinned) {
    joints.resize(count * 4);
    for (size_t i = 0; i < count; i++) {
      for (int b = 0; b < 4; b++)
        joints[i * 4 + b] = static_cast<uint16_t>(
            geometry.vertices[i].joints[b] & 0xffffu);
    }
  }

  filament::VertexBuffer::Builder builder;
  builder.vertexCount(static_cast<uint32_t>(count))
      .bufferCount(geometry.skinned ? 6 : 4)
      .attribute(filament::VertexAttribute::POSITION, 0,
                 filament::VertexBuffer::AttributeType::FLOAT3, 0,
                 sizeof(ModelVertex))
      .attribute(filament::VertexAttribute::TANGENTS, 1,
                 filament::VertexBuffer::AttributeType::FLOAT4, 0,
                 sizeof(filament::math::quatf))
      .attribute(filament::VertexAttribute::COLOR, 2,
                 filament::VertexBuffer::AttributeType::UBYTE4, 0, 4)
      .normalized(filament::VertexAttribute::COLOR)
      .attribute(filament::VertexAttribute::UV0, 3,
                 filament::VertexBuffer::AttributeType::FLOAT2, 0,
                 sizeof(glm::vec2));
  if (geometry.skinned) {
    builder.attribute(filament::VertexAttribute::BONE_INDICES, 4,
                      filament::VertexBuffer::AttributeType::USHORT4, 0, 8);
    builder.attribute(filament::VertexAttribute::BONE_WEIGHTS, 5,
                      filament::VertexBuffer::AttributeType::FLOAT4, 0,
                      sizeof(glm::vec4));
  }
  filament::VertexBuffer* vertices = builder.build(*engine);
  if (!vertices) return nullptr;

  const size_t position_bytes = sizeof(ModelVertex) * count;
  const size_t quat_bytes = sizeof(filament::math::quatf) * count;
  const size_t color_bytes = colors.size();
  const size_t uv_bytes = sizeof(glm::vec2) * count;
  vertices->setBufferAt(*engine, 0,
                        {CopyToHeap(geometry.vertices.data(), position_bytes),
                         position_bytes, DeleteUpload});
  vertices->setBufferAt(*engine, 1,
                        {CopyToHeap(quats.data(), quat_bytes), quat_bytes,
                         DeleteUpload});
  vertices->setBufferAt(*engine, 2,
                        {CopyToHeap(colors.data(), color_bytes), color_bytes,
                         DeleteUpload});
  vertices->setBufferAt(*engine, 3,
                        {CopyToHeap(uvs.data(), uv_bytes), uv_bytes,
                         DeleteUpload});
  if (geometry.skinned) {
    const size_t joint_bytes = joints.size() * sizeof(uint16_t);
    const size_t weight_bytes = count * sizeof(glm::vec4);
    vertices->setBufferAt(*engine, 4,
                          {CopyToHeap(joints.data(), joint_bytes), joint_bytes,
                           DeleteUpload});
    std::vector<glm::vec4> weights(count);
    for (size_t i = 0; i < count; i++)
      weights[i] = geometry.vertices[i].weights;
    vertices->setBufferAt(*engine, 5,
                          {CopyToHeap(weights.data(), weight_bytes),
                           weight_bytes, DeleteUpload});
  }

  filament::IndexBuffer* indices =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<uint32_t>(geometry.indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine);
  if (!indices) {
    engine->destroy(vertices);
    return nullptr;
  }
  const size_t index_bytes = geometry.indices.size() * sizeof(uint32_t);
  indices->setBuffer(
      *engine,
      {CopyToHeap(geometry.indices.data(), index_bytes), index_bytes,
       DeleteUpload});

  const glm::vec3 half = (geometry.bounds_max - geometry.bounds_min) * 0.5f;
  bounds->center = ToFilament(geometry.bounds_min + half);
  bounds->halfExtent = ToFilament(half);

  MeshEntry entry;
  entry.vertices = vertices;
  entry.indices = indices;
  entry.bounds = *bounds;
  meshes_.emplace(geometry.id, entry);
  return vertices;
}

void FilamentRenderer::DestroyProxy(filament::Engine* engine,
                                    entt::entity entity) {
  auto it = proxies_.find(entity);
  if (it == proxies_.end()) return;
  for (DrawRecord& draw : it->second.draws) {
    if (scene_ && draw.in_scene) scene_->remove(draw.renderable);
    engine->destroy(draw.renderable);
    engine->destroy(draw.material);
  }
  proxies_.erase(it);
}

void FilamentRenderer::BuildProxy(filament::Engine* engine, GameScene& game,
                                  entt::entity entity, const RenderMesh& mesh,
                                  const Transform& transform,
                                  ProxyRecord& record) {
  auto& registry = game.GetRegistry();
  const bool inactive = registry.all_of<InactiveTag>(entity);
  uint32_t layer = 0;
  if (auto* info = registry.try_get<NodeInfo>(entity))
    layer = static_cast<uint32_t>(info->layer);

  const ModelPose* pose = nullptr;
  if (mesh.pose_entity != entt::null && registry.valid(mesh.pose_entity)) {
    pose = registry.try_get<ModelPose>(mesh.pose_entity);
  }

  record.mesh = &mesh;
  for (DrawRecord& draw : record.draws) {
    if (scene_ && draw.in_scene) scene_->remove(draw.renderable);
    engine->destroy(draw.renderable);
    engine->destroy(draw.material);
  }
  record.draws.clear();
  record.visible = mesh.visible && !inactive;
  if (!record.visible) return;

  auto& renderables = engine->getRenderableManager();
  auto& transforms = engine->getTransformManager();
  const glm::mat4 world =
      glm::translate(glm::mat4(1.0f), transform.GetInterpolatedPosition()) *
      transform.GetRotationMatrix() *
      glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());

  static const RenderMaterial kDefault;
  std::vector<glm::mat4> skin;
  for (size_t draw_index = 0; draw_index < mesh.draws.size(); draw_index++) {
    const RenderDraw& draw = mesh.draws[draw_index];
    if (!draw.geometry || draw.geometry->indices.empty()) continue;
    const MeshGeometry& geometry = *draw.geometry;

    filament::VertexBuffer* vertices = nullptr;
    filament::IndexBuffer* indices = nullptr;
    filament::Box box;
    if (auto it = meshes_.find(geometry.id); it != meshes_.end()) {
      vertices = it->second.vertices;
      indices = it->second.indices;
      box = it->second.bounds;
    } else {
      vertices = UploadGeometry(engine, geometry, &box);
      if (!vertices) continue;
      it = meshes_.find(geometry.id);
      indices = it->second.indices;
      it->second.source = draw.geometry;
    }

    const RenderMaterial& material =
        mesh.materials.empty()
            ? kDefault
            : mesh.materials[std::min<size_t>(draw.material,
                                             mesh.materials.size() - 1)];

    const bool is_unlit = material.sdf_text || material.unlit;
    const bool is_transmissive = !is_unlit && material.transmission > 0.0f;
    const int surface = is_transmissive ? 2 : (material.transparent ? 1 : 0);
    filament::Material* base = material.sdf_text ? sdf_ : unlit_;
    if (!is_unlit) base = lit_[surface][material.double_sided ? 1 : 0];

    const filament::TextureSampler sampler(
        filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT);

    filament::MaterialInstance* instance = base->createInstance();
    instance->setParameter("baseColor",
                           ToFilament(glm::vec4(SrgbToLinear(
                                          glm::vec3(material.color)),
                                          material.color.a)));
    instance->setParameter("tiling", ToFilament(material.tiling));
    instance->setParameter("offset", ToFilament(material.offset));
    if (!is_unlit) {
      instance->setParameter("metallic",
                             std::clamp(material.metallic, 0.0f, 1.0f));
      instance->setParameter("roughness",
                             std::clamp(1.0f - material.smoothness, 0.05f,
                                        1.0f));
      instance->setParameter("emissive",
                             ToFilament(glm::vec4(material.emission, 1.0f)));
      filament::Texture* emissive_tex = white_;
      if (material.emission_texture) {
        if (filament::Texture* uploaded =
                UploadTexture(engine, *material.emission_texture)) {
          emissive_tex = uploaded;
        }
      }
      instance->setParameter("emissiveMap", emissive_tex, sampler);

      filament::Texture* normal_tex = flat_normal_;
      bool has_normal = false;
      if (material.normal_texture) {
        if (filament::Texture* uploaded =
                UploadTexture(engine, *material.normal_texture)) {
          normal_tex = uploaded;
          has_normal = true;
        }
      }
      instance->setParameter("normalMap", normal_tex, sampler);
      instance->setParameter("normalScale",
                             has_normal ? material.normal_scale : 0.0f);

      if (is_transmissive) {
        const glm::vec3 trans_color =
            glm::clamp(material.transmission_color, 1e-4f, 1.0f);
        const bool tinted = trans_color != glm::vec3(1.0f);
        const float thickness = material.thickness > 0.0f
                                    ? material.thickness
                                    : (tinted ? 0.01f : 0.0f);
        const filament::math::float3 absorption =
            tinted ? filament::Color::absorptionAtDistance(
                         ToFilament(trans_color), thickness)
                   : filament::math::float3{0.0f, 0.0f, 0.0f};
        instance->setParameter("transmission",
                               std::clamp(material.transmission, 0.0f, 1.0f));
        instance->setParameter("ior", std::max(material.ior, 1.0f));
        instance->setParameter("thickness", thickness);
        instance->setParameter("absorption", absorption);
      }
    }
    // The sampler must always be bound (Filament panics on unbound
    // samplers at draw time), so textures without an image show white.
    filament::Texture* texture = white_;
    if (material.texture) {
      if (filament::Texture* uploaded = UploadTexture(engine, *material.texture))
        texture = uploaded;
    }
    instance->setParameter("baseColorMap", texture, sampler);

    glm::mat4 draw_world = world;
    std::vector<filament::math::mat4f> bones;
    const bool skinned = draw.skin >= 0;
    if (skinned) {
      mesh.SkinMatrices(draw, pose, skin);
      bones.reserve(skin.size());
      for (const glm::mat4& bone : skin) bones.push_back(ToFilament(bone));
    } else {
      draw_world = world * mesh.DrawMatrix(draw, pose);
    }

    utils::Entity renderable = utils::EntityManager::get().create();
    filament::RenderableManager::Builder build(1);
    build.boundingBox(box)
        .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                  vertices, indices)
        .material(0, instance)
        .culling(true)
        .castShadows(mesh.cast_shadows)
        .layerMask(0xff, 1u << (layer & 7));
    if (skinned) build.skinning(bones.size());
    if (build.build(*engine, renderable) !=
        filament::RenderableManager::Builder::Success) {
      engine->destroy(renderable);
      engine->destroy(instance);
      continue;
    }
    transforms.setTransform(transforms.getInstance(renderable),
                            ToFilament(draw_world));
    if (skinned) {
      renderables.setBones(renderables.getInstance(renderable), bones.data(),
                            bones.size());
    }
    scene_->addEntity(renderable);

    DrawRecord record_draw;
    record_draw.renderable = renderable;
    record_draw.material = instance;
    record_draw.layer = layer;
    record_draw.skinned = skinned;
    record_draw.draw_index = static_cast<uint32_t>(draw_index);
    record_draw.in_scene = true;
    record.draws.push_back(record_draw);
  }
}

void FilamentRenderer::SyncScene(GameScene& game) {
  filament::Engine* engine = engine_;
  auto& registry = game.GetRegistry();
  auto& transforms = engine->getTransformManager();
  auto& renderables = engine->getRenderableManager();
  frame_++;
  SweepGarbage(engine, false);

  // Entities that went away.
  std::vector<entt::entity> gone;
  for (auto& [entity, record] : proxies_) {
    if (!registry.valid(entity)) gone.push_back(entity);
  }
  for (entt::entity entity : gone) DestroyProxy(engine, entity);

  std::vector<glm::mat4> skin;
  for (auto [entity, mesh, transform] :
       registry.view<RenderMesh, Transform>().each()) {
    ProxyRecord& record = proxies_[entity];
    const glm::mat4 world =
        glm::translate(glm::mat4(1.0f),
                       transform.GetInterpolatedPosition()) *
        transform.GetRotationMatrix() *
        glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());
    const bool inactive = registry.all_of<InactiveTag>(entity);
    const bool visible = mesh.visible && !inactive;
    const bool moved = world != record.world;
    const ModelPose* pose = nullptr;
    if (mesh.pose_entity != entt::null &&
        registry.valid(mesh.pose_entity)) {
      pose = registry.try_get<ModelPose>(mesh.pose_entity);
    }

    if (record.mesh != &mesh || (!record.visible && visible)) {
      // A different RenderMesh (added, edited, replaced), or one that was
      // hidden when it was built: rebuild this entity's draws.
      BuildProxy(engine, game, entity, mesh, transform, record);
      record.world = world;
      continue;
    }

    if (visible != record.visible) {
      for (DrawRecord& draw : record.draws) {
        if (visible && !draw.in_scene) {
          scene_->addEntity(draw.renderable);
          draw.in_scene = true;
        } else if (!visible && draw.in_scene) {
          scene_->remove(draw.renderable);
          draw.in_scene = false;
        }
      }
      record.visible = visible;
    }

    if (!moved && !pose) continue;
    record.world = world;

    // What moved: rigid draws shift with the entity (their node matrix is
    // folded into the transform), skinned ones carry their own bones.
    for (DrawRecord& draw : record.draws) {
      const RenderDraw& render = mesh.draws[draw.draw_index];
      if (draw.skinned) {
        mesh.SkinMatrices(render, pose, skin);
        std::vector<filament::math::mat4f> bones(skin.size());
        for (size_t i = 0; i < skin.size(); i++)
          bones[i] = ToFilament(skin[i]);
        renderables.setBones(renderables.getInstance(draw.renderable),
                             bones.data(), bones.size());
      } else {
        const glm::mat4 draw_world = world * mesh.DrawMatrix(render, pose);
        transforms.setTransform(transforms.getInstance(draw.renderable),
                                ToFilament(draw_world));
      }
    }
  }

  // Expired uploads (a map change, freed textures) go away once no frame in
  // flight can still be using them.
  if (frame_ % kSweepEvery == 0) {
    std::erase_if(textures_, [&](auto& entry) {
      if (!entry.second.source.expired()) return false;
      garbage_textures_.emplace_back(frame_, entry.second.texture);
      return true;
    });
    std::erase_if(meshes_, [&](auto& entry) {
      if (!entry.second.source.expired()) return false;
      engine->destroy(entry.second.vertices);
      engine->destroy(entry.second.indices);
      return true;
    });
  }

  SyncLights(engine, game);
}

void FilamentRenderer::SyncLights(filament::Engine* engine, GameScene& game) {
  const SceneLighting& lighting = game.GetLighting();

  const glm::vec3 direction =
      glm::length(lighting.direction) < 1e-4f
          ? glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f))
          : glm::normalize(lighting.direction);
  const glm::vec3 color = glm::max(lighting.color, glm::vec3(0.0f));
  const float shadow_distance = std::max(lighting.shadow_distance, 1.0f);
  const bool sun_changed = direction != sun_direction_ || color != sun_color_ ||
                           lighting.cast_shadows != sun_shadows_ ||
                           shadow_distance != shadow_distance_ ||
                           lighting.shadow_bias != shadow_bias_ ||
                           lighting.shadow_normal_bias != shadow_normal_bias_ ||
                           !sun_.getId();
  if (sun_changed) {
    if (sun_.getId()) {
      scene_->remove(sun_);
      engine->destroy(sun_);
    }
    const float intensity =
        std::max({color.r, color.g, color.b, 1e-6f});
    filament::LightManager::ShadowOptions shadow;
    shadow.mapSize = 2048;
    shadow.shadowCascades = 4;
    filament::LightManager::ShadowCascades::computePracticalSplits(
        shadow.cascadeSplitPositions, shadow.shadowCascades, 0.1f,
        shadow_distance, 0.5f);
    shadow.shadowFar = shadow_distance;
    shadow.shadowNearHint = 0.5f;
    shadow.shadowFarHint = shadow_distance;
    shadow.constantBias = std::clamp(lighting.shadow_bias, 0.0f, 0.05f);
    shadow.normalBias =
        std::clamp(lighting.shadow_normal_bias * 25.0f, 0.25f, 2.5f);
    sun_ = utils::EntityManager::get().create();
    filament::LightManager::Builder(filament::LightManager::Type::SUN)
        .color(ToFilament(color / intensity))
        .intensity(intensity * kSunLuxPerIntensity)
        .direction(ToFilament(direction))
        .castShadows(lighting.cast_shadows)
        .shadowOptions(shadow)
        .build(*engine, sun_);
    scene_->addEntity(sun_);
    sun_direction_ = direction;
    sun_color_ = color;
    sun_shadows_ = lighting.cast_shadows;
    shadow_distance_ = shadow_distance;
    shadow_bias_ = lighting.shadow_bias;
    shadow_normal_bias_ = lighting.shadow_normal_bias;
  }

  bool points_changed = lighting.point_lights.size() != points_.size();
  for (size_t i = 0; !points_changed && i < point_values_.size(); i++) {
    const PointLightData& a = lighting.point_lights[i];
    const PointLightData& b = point_values_[i];
    if (a.position != b.position || a.color != b.color ||
        a.range != b.range) {
      points_changed = true;
    }
  }
  if (points_changed) {
    for (utils::Entity point : points_) {
      scene_->remove(point);
      engine->destroy(point);
    }
    points_.clear();
    for (const PointLightData& light : lighting.point_lights) {
      const float intensity =
          std::max({light.color.r, light.color.g, light.color.b, 1e-6f});
      utils::Entity entity = utils::EntityManager::get().create();
      filament::LightManager::Builder(filament::LightManager::Type::POINT)
          .position(ToFilament(light.position))
          .color(ToFilament(light.color / intensity))
          .intensity(intensity * kPointLumensPerIntensity)
          .build(*engine, entity);
      scene_->addEntity(entity);
      points_.push_back(entity);
    }
    point_values_ = lighting.point_lights;
  }

  const std::string& skybox_path = game.GetSkyboxPath();
  if (!skybox_synced_ || skybox_path != skybox_path_) {
    SyncSkybox(engine, skybox_path);
  }

  // Ambient hemisphere light as band-1 spherical harmonics scaled to match the
  // physical sun lux range when no skybox panorama provides an IBL.
  if (!has_panorama_ibl_ && (lighting.ambient != ambient_sky_ ||
                             lighting.ambient_ground != ambient_ground_)) {
    if (ambient_) {
      scene_->setIndirectLight(nullptr);
      garbage_indirect_lights_.emplace_back(frame_, ambient_);
      ambient_ = nullptr;
    }
    const glm::vec3 sky = SrgbToLinear(lighting.ambient);
    const glm::vec3 ground = SrgbToLinear(lighting.ambient_ground);
    const filament::math::float3 sh[4] = {
        ToFilament((sky + ground) * 0.5f),
        ToFilament((sky - ground) * 0.5f),
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
    };
    ambient_ = filament::IndirectLight::Builder()
                  .irradiance(2, sh)
                  .intensity(kSunLuxPerIntensity / glm::pi<float>())
                  .build(*engine);
    scene_->setIndirectLight(ambient_);
    ambient_sky_ = lighting.ambient;
    ambient_ground_ = lighting.ambient_ground;
  }
}

void FilamentRenderer::SyncSkybox(filament::Engine* engine,
                                  const std::string& path) {
  skybox_path_ = path;
  skybox_synced_ = true;

  // Retire the current panorama resources (destroyed after 3 frames).
  if (scene_) scene_->setSkybox(fallback_sky_);
  if (panorama_sky_) {
    garbage_skyboxes_.emplace_back(frame_, panorama_sky_);
    panorama_sky_ = nullptr;
  }
  if (has_panorama_ibl_ && ambient_) {
    if (scene_) scene_->setIndirectLight(nullptr);
    garbage_indirect_lights_.emplace_back(frame_, ambient_);
    ambient_ = nullptr;
  }
  if (sky_cube_) {
    garbage_textures_.emplace_back(frame_, sky_cube_);
    sky_cube_ = nullptr;
  }
  if (ibl_reflections_) {
    garbage_textures_.emplace_back(frame_, ibl_reflections_);
    ibl_reflections_ = nullptr;
  }
  if (ibl_irradiance_) {
    garbage_textures_.emplace_back(frame_, ibl_irradiance_);
    ibl_irradiance_ = nullptr;
  }
  has_panorama_ibl_ = false;
  ambient_sky_ = glm::vec3(-1.0f);
  ambient_ground_ = glm::vec3(-1.0f);

  if (path.empty()) return;

  std::shared_ptr<ImageData> image = ImageData::Load(path);
  if (!image || image->pixels.empty() || image->width <= 0 ||
      image->height <= 0) {
    PDebug::warn("Filament: could not load skybox panorama {}", path);
    return;
  }
  if (image->width != 2 * image->height) {
    PDebug::warn(
        "Filament: skybox panorama {} ({}x{}) is not 2:1 equirectangular", path,
        image->width, image->height);
    return;
  }

  const uint8_t levels = std::max<uint8_t>(
      1, static_cast<uint8_t>(std::ilogbf(static_cast<float>(image->width)) + 1));
  filament::Texture* equirect =
      filament::Texture::Builder()
          .width(static_cast<uint32_t>(image->width))
          .height(static_cast<uint32_t>(image->height))
          .levels(levels)
          .usage(filament::Texture::Usage::SAMPLEABLE |
                 filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::GEN_MIPMAPPABLE)
          .format(filament::Texture::InternalFormat::SRGB8_A8)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);
  if (!equirect) return;
  const size_t bytes = image->pixels.size();
  equirect->setImage(
      *engine, 0,
      filament::Texture::PixelBufferDescriptor(
          CopyToHeap(image->pixels.data(), bytes), bytes,
          filament::backend::PixelDataFormat::RGBA,
          filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0, DeleteUpload));

  IBLPrefilterContext prefilter(*engine);
  IBLPrefilterContext::EquirectangularToCubemap to_cube(prefilter);
  IBLPrefilterContext::SpecularFilter specular(prefilter);
  IBLPrefilterContext::IrradianceFilter irradiance(prefilter);

  sky_cube_ = to_cube(equirect);
  garbage_textures_.emplace_back(frame_, equirect);
  if (!sky_cube_) return;

  ibl_reflections_ = specular(sky_cube_);
  IBLPrefilterContext::IrradianceFilter::Options irr_options;
  irr_options.generateMipmap = false;
  ibl_irradiance_ = irradiance(irr_options, sky_cube_);

  panorama_sky_ = filament::Skybox::Builder().environment(sky_cube_).build(*engine);
  ambient_ = filament::IndirectLight::Builder()
                 .reflections(ibl_reflections_)
                 .irradiance(ibl_irradiance_)
                 .intensity(kSunLuxPerIntensity / glm::pi<float>())
                 .build(*engine);
  if (scene_) {
    scene_->setSkybox(panorama_sky_);
    scene_->setIndirectLight(ambient_);
  }
  has_panorama_ibl_ = true;
}

void FilamentRenderer::DestroyLights(filament::Engine* engine) {
  if (sun_.getId()) {
    if (scene_) scene_->remove(sun_);
    engine->destroy(sun_);
    sun_ = {};
  }
  for (utils::Entity point : points_) {
    if (scene_) scene_->remove(point);
    engine->destroy(point);
  }
  points_.clear();
  point_values_.clear();
  if (scene_) {
    scene_->setSkybox(nullptr);
    scene_->setIndirectLight(nullptr);
  }
  if (panorama_sky_) {
    engine->destroy(panorama_sky_);
    panorama_sky_ = nullptr;
  }
  if (ambient_) {
    engine->destroy(ambient_);
    ambient_ = nullptr;
  }
  if (sky_cube_) {
    engine->destroy(sky_cube_);
    sky_cube_ = nullptr;
  }
  if (ibl_reflections_) {
    engine->destroy(ibl_reflections_);
    ibl_reflections_ = nullptr;
  }
  if (ibl_irradiance_) {
    engine->destroy(ibl_irradiance_);
    ibl_irradiance_ = nullptr;
  }
  skybox_path_.clear();
  skybox_synced_ = false;
  has_panorama_ibl_ = false;
  ambient_sky_ = glm::vec3(-1.0f);
  ambient_ground_ = glm::vec3(-1.0f);
}

}  // namespace Mercury::fil
