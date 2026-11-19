// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "filament_renderer.h"

#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <filament/Color.h>
#include <filament/Exposure.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/Viewport.h>
#include <geometry/SurfaceOrientation.h>
#include <stb/stb_image.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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
// multipliers around 1. The sun anchors the scale at Filament's reference
// 100000 lux. A point light's intensity is luminous power in lumens, so its
// illuminance at distance d is power / (4*pi*d^2); multiplying by 4*pi makes
// an intensity-1 point at 1 m match an intensity-1 sun.
constexpr float kSunLuxPerIntensity = 100000.0f;
constexpr float kPointLumensPerIntensity =
    4.0f * glm::pi<float>() * kSunLuxPerIntensity;
// An authored radiance R maps to R * kSunLuxPerIntensity / pi nits at the sun
// anchor: a Lambertian surface lit to the sun's 100000 lux has radiance
// 100000/pi (nits are candela per square metre, i.e. lux/pi for a Lambertian
// emitter). Lit emissive is converted so it shares the lights' pre-exposed
// scale; Filament then applies the camera exposure (emissive.w = 1).
constexpr float kLuminancePerIntensity = kSunLuxPerIntensity / glm::pi<float>();
// How often expired mesh/texture sources are swept away.
constexpr uint64_t kSweepEvery = 30;
// The room probe is captured once the scene has been still for this many
// frames, and no more often than every kProbeMinInterval frames.
constexpr uint64_t kProbeQuietFrames = 3;
constexpr uint64_t kProbeMinInterval = 30;
// Glass below this index of refraction hardly bends the view; it keeps
// screen-space refraction (to show what is inside it) even with a probe.
constexpr float kCubemapRefractionMinIor = 1.1f;

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

// Shared per-light shadow settings. Filament derives a soft penumbra from
// the light's physical size when the View uses ShadowType::PCSS.
filament::LightManager::ShadowOptions MakeShadowOptions(float bias,
                                                        float bulb_radius) {
  filament::LightManager::ShadowOptions options;
  options.mapSize = 1024;
  options.constantBias = std::clamp(bias, 0.0f, 0.05f);
  options.shadowBulbRadius = std::max(bulb_radius, 0.01f);
  return options;
}

// Filament's matrices are column-major like glm's, so the scalar
// constructor takes glm's column order directly.
filament::math::float2 ToFilament(const glm::vec2& v) { return {v.x, v.y}; }
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
std::vector<filament::math::quatf> PackTangents(const MeshGeometry& source) {
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
  std::unique_ptr<filament::geometry::SurfaceOrientation> orientation(
      builder.build());
  std::vector<filament::math::quatf> quats(count);
  if (orientation) {
    orientation->getQuats(quats.data(), count, sizeof(filament::math::quatf));
  } else {
    // No usable tangent data: identity frames (the normal still reads).
    for (size_t i = 0; i < count; i++) quats[i] = {1.0f, 0.0f, 0.0f, 0.0f};
  }
  return quats;
}

}  // namespace

// The IBL prefilter context and its filters, made once (each builds
// materials and a kernel texture) and shared by the skybox and the probe.
struct FilamentRenderer::Prefilter {
  explicit Prefilter(filament::Engine& engine)
      : context(engine),
        to_cube(context),
        specular(context),
        irradiance(context) {}
  IBLPrefilterContext context;
  IBLPrefilterContext::EquirectangularToCubemap to_cube;
  IBLPrefilterContext::SpecularFilter specular;
  IBLPrefilterContext::IrradianceFilter irradiance;
};

FilamentRenderer::FilamentRenderer() = default;
FilamentRenderer::~FilamentRenderer() = default;

FilamentRenderer::Prefilter& FilamentRenderer::EnsurePrefilter(
    filament::Engine* engine) {
  if (!prefilter_) prefilter_ = std::make_unique<Prefilter>(*engine);
  return *prefilter_;
}

bool FilamentRenderer::Init(filament::Engine* engine) {
  engine_ = engine;
  lit_[0][0] = LoadMaterial("lit_opaque");
  lit_[0][1] = LoadMaterial("lit_opaque_double_sided");
  lit_[1][0] = LoadMaterial("lit_transparent");
  lit_[1][1] = LoadMaterial("lit_transparent_double_sided");
  lit_[2][0] = LoadMaterial("lit_transmissive");
  lit_[2][1] = LoadMaterial("lit_transmissive_double_sided");
  lit_cubemap_[0] = LoadMaterial("lit_transmissive_cubemap");
  lit_cubemap_[1] = LoadMaterial("lit_transmissive_cubemap_double_sided");
  unlit_ = LoadMaterial("unlit");
  unlit_transparent_ = LoadMaterial("unlit_transparent");
  sdf_ = LoadMaterial("text_sdf");
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 2; j++) {
      if (!lit_[i][j]) return false;
    }
  }
  if (!lit_cubemap_[0] || !lit_cubemap_[1]) return false;
  if (!unlit_ || !unlit_transparent_ || !sdf_) return false;
  scene_ = engine_->createScene();

  // The texture shown when a material has no image: 1x1 white, plus a 1x1
  // flat tangent-space normal. Filament panics at draw time on unbound
  // samplers, so every material sampler is always bound to something.
  auto make_1x1 = [&](filament::Texture::InternalFormat format,
                      const uint8_t pixel[4]) {
    filament::Texture* tex =
        filament::Texture::Builder()
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

void FilamentRenderer::SetSkyboxForClear(bool show_panorama) {
  if (!scene_) return;
  scene_->setSkybox(show_panorama && panorama_sky_ ? panorama_sky_
                                                   : fallback_sky_);
}

void FilamentRenderer::SetRenderTextures(
    const std::unordered_map<std::string, filament::Texture*>& textures) {
  if (textures == render_textures_) return;
  render_textures_ = textures;
  const filament::TextureSampler rt_sampler(
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
  for (auto& [entity, record] : proxies_) {
    for (DrawRecord& draw : record.draws) {
      if (draw.render_texture.empty() || !draw.material) continue;
      auto it = render_textures_.find(draw.render_texture);
      filament::Texture* tex =
          (it != render_textures_.end() && it->second) ? it->second : white_;
      if (tex) draw.material->setParameter("baseColorMap", tex, rt_sampler);
    }
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
  render_textures_.clear();
  DestroyLights(engine);
  RetireProbeLight();
  DestroyProbeTargets(engine);
  SweepGarbage(engine, true);
  prefilter_.reset();
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
  for (filament::Material*& cubemap : lit_cubemap_) {
    if (cubemap) engine->destroy(cubemap);
    cubemap = nullptr;
  }
  if (unlit_) {
    engine->destroy(unlit_);
    unlit_ = nullptr;
  }
  if (unlit_transparent_) {
    engine->destroy(unlit_transparent_);
    unlit_transparent_ = nullptr;
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
    filament::Engine* engine, const std::shared_ptr<ImageData>& image) {
  if (!image) return nullptr;
  if (auto it = textures_.find(image->id); it != textures_.end()) {
    it->second.source = image;
    return it->second.texture;
  }
  if (image->pixels.empty()) return nullptr;

  filament::Texture* texture =
      filament::Texture::Builder()
          .width(static_cast<uint32_t>(image->width))
          .height(static_cast<uint32_t>(image->height))
          .levels(0xFF)
          .usage(filament::Texture::Usage::SAMPLEABLE |
                 filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::GEN_MIPMAPPABLE)
          .format(image->srgb ? filament::Texture::InternalFormat::SRGB8_A8
                              : filament::Texture::InternalFormat::RGBA8)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);
  if (!texture) return nullptr;
  const size_t bytes = image->pixels.size();
  texture->setImage(
      *engine, 0,
      filament::Texture::PixelBufferDescriptor(
          CopyToHeap(image->pixels.data(), bytes), bytes,
          filament::backend::PixelDataFormat::RGBA,
          filament::backend::PixelDataType::UBYTE, 1, 0, 0, 0, DeleteUpload));
  texture->generateMipmaps(*engine);

  TextureEntry entry;
  entry.texture = texture;
  entry.source = image;
  textures_.emplace(image->id, entry);
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
        joints[i * 4 + b] =
            static_cast<uint16_t>(geometry.vertices[i].joints[b] & 0xffffu);
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
  vertices->setBufferAt(
      *engine, 1,
      {CopyToHeap(quats.data(), quat_bytes), quat_bytes, DeleteUpload});
  vertices->setBufferAt(
      *engine, 2,
      {CopyToHeap(colors.data(), color_bytes), color_bytes, DeleteUpload});
  vertices->setBufferAt(
      *engine, 3, {CopyToHeap(uvs.data(), uv_bytes), uv_bytes, DeleteUpload});
  if (geometry.skinned) {
    const size_t joint_bytes = joints.size() * sizeof(uint16_t);
    const size_t weight_bytes = count * sizeof(glm::vec4);
    vertices->setBufferAt(
        *engine, 4,
        {CopyToHeap(joints.data(), joint_bytes), joint_bytes, DeleteUpload});
    std::vector<glm::vec4> weights(count);
    for (size_t i = 0; i < count; i++)
      weights[i] = geometry.vertices[i].weights;
    vertices->setBufferAt(
        *engine, 5,
        {CopyToHeap(weights.data(), weight_bytes), weight_bytes, DeleteUpload});
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
  indices->setBuffer(*engine, {CopyToHeap(geometry.indices.data(), index_bytes),
                               index_bytes, DeleteUpload});

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
  scene_revision_++;
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
  scene_revision_++;
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
        mesh.materials.empty() ? kDefault
                               : mesh.materials[std::min<size_t>(
                                     draw.material, mesh.materials.size() - 1)];

    const bool is_unlit = material.sdf_text || material.unlit;
    const bool is_transmissive = !is_unlit && material.transmission > 0.0f;
    const int surface = is_transmissive ? 2 : (material.transparent ? 1 : 0);
    // Solid refraction needs a path length through the object. Without one
    // from the material, take the object's thinnest world extent (the
    // bounding box's edges under the full world and draw matrix, so rotation,
    // node scale and unit conversion count): a sphere's diameter, a slab's
    // depth. A very flat shape is a pane (a slab), not a solid, and gets the
    // minimum; the rest is capped so a large object cannot refract by a huge
    // offset. Colour does not depend on it, only the refraction offset.
    float thickness = material.thickness;
    bool slab = false;
    if (is_transmissive && thickness <= 0.0f) {
      const glm::mat4 box_world =
          draw.skin >= 0 ? world : world * mesh.DrawMatrix(draw, pose);
      const glm::vec3 half(box.halfExtent.x, box.halfExtent.y,
                           box.halfExtent.z);
      const glm::vec3 extent(
          glm::length(glm::vec3(box_world[0]) * (half.x * 2.0f)),
          glm::length(glm::vec3(box_world[1]) * (half.y * 2.0f)),
          glm::length(glm::vec3(box_world[2]) * (half.z * 2.0f)));
      const float smallest =
          std::max(std::min({extent.x, extent.y, extent.z}), 1e-6f);
      const float largest = std::max({extent.x, extent.y, extent.z});
      thickness = largest / smallest > 10.0f
                      ? 0.001f
                      : std::clamp(smallest, 0.001f, 2.0f);
      // Flat-ish shapes (panes, a sheet of water) are no solid to look
      // into; a sphere or a box is within a ratio of about 2 or 3.
      slab = largest / smallest > 4.0f;
    }

    filament::Material* base =
        material.sdf_text
            ? sdf_
            : (material.transparent ? unlit_transparent_ : unlit_);
    if (!is_unlit) {
      const int side = material.double_sided ? 1 : 0;
      // With a room probe placed, solid glass refracts the probe's cubemap
      // (the whole room, which the screen cannot show); without one, and for
      // panes and water sheets that barely bend the view, it refracts what
      // is on screen. The probe sees a pane from one side only, so a cubemap
      // pane would refract its own capture and go dark. A shell with an ior
      // near 1 does not bend the view at all: a cubemap would show the room
      // behind it instead of what is inside, so it stays on screen too.
      base = is_transmissive && probe_refraction_ && !slab &&
                     material.ior > kCubemapRefractionMinIor
                 ? lit_cubemap_[side]
                 : lit_[surface][side];
    }

    const filament::TextureSampler sampler(
        filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT);

    filament::MaterialInstance* instance = base->createInstance();
    instance->setParameter(
        "baseColor",
        ToFilament(glm::vec4(SrgbToLinear(glm::vec3(material.color)),
                             material.color.a)));
    instance->setParameter("tiling", ToFilament(material.tiling));
    instance->setParameter("offset", ToFilament(material.offset));
    if (!is_unlit) {
      instance->setParameter("metallic",
                             std::clamp(material.metallic, 0.0f, 1.0f));
      instance->setParameter("roughness",
                             std::clamp(1.0f - material.smoothness,
                                        is_transmissive ? 0.0f : 0.05f, 1.0f));
      // Transmissive materials take F0 from their ior and have no
      // reflectance input (setting it would override the ior).
      if (!is_transmissive)
        instance->setParameter("reflectance",
                               std::clamp(material.reflectance, 0.0f, 1.0f));

      filament::Texture* normal_tex = flat_normal_;
      bool has_normal = false;
      if (material.normal_texture) {
        if (filament::Texture* uploaded =
                UploadTexture(engine, material.normal_texture)) {
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
        // The light crosses the volume twice (in and out), so the colour is
        // the transmittance over the full path: sqrt(tint) at one crossing.
        const filament::math::float3 absorption =
            tinted ? filament::Color::absorptionAtDistance(
                         ToFilament(trans_color), thickness * 2.0f)
                   : filament::math::float3{0.0f, 0.0f, 0.0f};
        instance->setParameter("transmission",
                               std::clamp(material.transmission, 0.0f, 1.0f));
        instance->setParameter("ior", std::max(material.ior, 1.0f));
        instance->setParameter("thickness", thickness);
        instance->setParameter("absorption", absorption);
      }
    }
    // Filament treats emissive.rgb as nits and multiplies it by the camera
    // exposure when the alpha (the exposure weight) is 1. Convert the
    // authored radiance so lit and emissive surfaces share the lights'
    // pre-exposed scale; w = 0 would leave emission at display scale and
    // make probe captures (post-processing off) 1/exposure too bright.
    // Unlit surfaces support emission too, so this applies to both; the SDF
    // text material has no emissive parameter.
    if (!material.sdf_text) {
      instance->setParameter(
          "emissive", ToFilament(glm::vec4(
                          material.emission * kLuminancePerIntensity, 1.0f)));
      filament::Texture* emissive_tex = white_;
      if (material.emission_texture) {
        if (filament::Texture* uploaded =
                UploadTexture(engine, material.emission_texture)) {
          emissive_tex = uploaded;
        }
      }
      instance->setParameter("emissiveMap", emissive_tex, sampler);
    }
    // The sampler must always be bound (Filament panics on unbound
    // samplers at draw time), so textures without an image show white.
    filament::Texture* texture = white_;
    filament::TextureSampler base_sampler = sampler;
    if (!material.render_texture.empty()) {
      auto rt_it = render_textures_.find(material.render_texture);
      if (rt_it != render_textures_.end() && rt_it->second) {
        texture = rt_it->second;
      }
      base_sampler = filament::TextureSampler(
          filament::TextureSampler::MagFilter::LINEAR,
          filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    } else if (material.texture) {
      if (filament::Texture* uploaded = UploadTexture(engine, material.texture))
        texture = uploaded;
    }
    instance->setParameter("baseColorMap", texture, base_sampler);

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
    record_draw.transmissive = is_transmissive;
    if (!material.sdf_text && glm::length(material.emission) > 0.0f)
      record_draw.emission =
          material.emission * kLuminancePerIntensity;  // nits, as set above
    record_draw.draw_index = static_cast<uint32_t>(draw_index);
    record_draw.in_scene = true;
    record_draw.render_texture = material.render_texture;
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

  // Glass refracts the probe's cubemap while a probe is placed and the
  // screen otherwise; when that flips, transmissive draws change material.
  const bool refract_probe =
      game.GetLighting().probes.placed && game.GetLighting().probes.enabled;
  if (refract_probe != probe_refraction_) {
    probe_refraction_ = refract_probe;
    for (auto& [entity, record] : proxies_) {
      for (const DrawRecord& draw : record.draws) {
        if (draw.transmissive) {
          record.mesh = nullptr;  // rebuilt below
          break;
        }
      }
    }
  }

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
        glm::translate(glm::mat4(1.0f), transform.GetInterpolatedPosition()) *
        transform.GetRotationMatrix() *
        glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());
    const bool inactive = registry.all_of<InactiveTag>(entity);
    const bool visible = mesh.visible && !inactive;
    const bool moved = world != record.world;
    const ModelPose* pose = nullptr;
    if (mesh.pose_entity != entt::null && registry.valid(mesh.pose_entity)) {
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
      scene_revision_++;
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
    // Only still scenery is worth a new capture: bodies under physics,
    // skinned meshes and posed models move every frame and would never let
    // the probe settle (or recapture continuously).
    if (moved) {
      bool dynamic = pose != nullptr;
      if (const Rigidbody* body = registry.try_get<Rigidbody>(entity)) {
        dynamic = dynamic || body->motion_type != MotionType::kStatic;
      }
      for (const DrawRecord& draw : record.draws)
        dynamic = dynamic || draw.skinned;
      if (!dynamic) scene_revision_++;
    }

    // What moved: rigid draws shift with the entity (their node matrix is
    // folded into the transform), skinned ones carry their own bones.
    for (DrawRecord& draw : record.draws) {
      const RenderDraw& render = mesh.draws[draw.draw_index];
      if (draw.skinned) {
        mesh.SkinMatrices(render, pose, skin);
        std::vector<filament::math::mat4f> bones(skin.size());
        for (size_t i = 0; i < skin.size(); i++) bones[i] = ToFilament(skin[i]);
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
  SyncProbe(engine, game);
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
    scene_revision_++;
    if (sun_.getId()) {
      scene_->remove(sun_);
      engine->destroy(sun_);
    }
    const float intensity = std::max({color.r, color.g, color.b, 1e-6f});
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
    if (a.position != b.position || a.color != b.color || a.range != b.range) {
      points_changed = true;
    }
  }
  if (points_changed) {
    scene_revision_++;
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
          .falloff(std::max(light.range, 0.1f))
          .castShadows(light.shadows != 0)
          .shadowOptions(MakeShadowOptions(light.shadow_bias, light.radius))
          .build(*engine, entity);
      scene_->addEntity(entity);
      points_.push_back(entity);
    }
    point_values_ = lighting.point_lights;
  }

  const size_t area_count = std::min<size_t>(lighting.area_lights.size(), 4);
  bool areas_changed = area_count != area_values_.size();
  for (size_t i = 0; !areas_changed && i < area_values_.size(); i++) {
    const AreaLightData& a = lighting.area_lights[i];
    const AreaLightData& b = area_values_[i];
    if (a.position != b.position || a.right != b.right || a.up != b.up ||
        a.color != b.color || a.range != b.range ||
        a.two_sided != b.two_sided || a.shadows != b.shadows) {
      areas_changed = true;
    }
  }
  if (areas_changed) {
    scene_revision_++;
    for (utils::Entity area : areas_) {
      scene_->remove(area);
      engine->destroy(area);
    }
    areas_.clear();
    area_values_.assign(lighting.area_lights.begin(),
                        lighting.area_lights.begin() + area_count);
    for (const AreaLightData& light : area_values_) {
      const glm::vec3 cross = glm::cross(light.right, light.up);
      const glm::vec3 dir = glm::length(cross) < 1e-4f
                                ? glm::vec3(0.0f, -1.0f, 0.0f)
                                : glm::normalize(cross);
      const float intensity =
          std::max({light.color.r, light.color.g, light.color.b, 1e-6f});
      const float area =
          4.0f * glm::length(light.right) * glm::length(light.up);
      // The authored intensity is radiance R. At the sun anchor its luminance
      // is R * kSunLuxPerIntensity / pi nits, so a Lambertian rectangle of
      // area A emits pi * luminance * A = R * kSunLuxPerIntensity * A lumens.
      const float lumens = intensity * kSunLuxPerIntensity * area;
      const float bulb =
          std::max(glm::length(light.right), glm::length(light.up));
      auto add_spot = [&](const glm::vec3& spot_dir, bool shadows) {
        utils::Entity entity = utils::EntityManager::get().create();
        filament::LightManager::Builder(filament::LightManager::Type::SPOT)
            .position(ToFilament(light.position))
            .direction(ToFilament(spot_dir))
            .color(ToFilament(light.color / intensity))
            .intensity(lumens)
            .falloff(std::max(light.range, 0.1f))
            .spotLightCone(glm::radians(35.0f), glm::radians(75.0f))
            .castShadows(shadows)
            .shadowOptions(MakeShadowOptions(light.shadow_bias, bulb))
            .build(*engine, entity);
        scene_->addEntity(entity);
        areas_.push_back(entity);
      };
      add_spot(dir, light.shadows != 0);
      if (light.two_sided) add_spot(-dir, false);
    }
  }

  const std::string& skybox_path = game.GetSkyboxPath();
  if (!skybox_synced_ || skybox_path != skybox_path_) {
    SyncSkybox(engine, skybox_path, lighting.environment_intensity);
  } else if (has_panorama_ibl_ && ambient_ &&
             lighting.environment_intensity != env_intensity_) {
    // Filament scales the whole IBL; no need to rebuild the prefiltered
    // textures when only the brightness changes.
    ambient_->setIntensity(lighting.environment_intensity);
    env_intensity_ = lighting.environment_intensity;
    scene_revision_++;
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
    scene_revision_++;
    ambient_sky_ = lighting.ambient;
    ambient_ground_ = lighting.ambient_ground;
  }
}

void FilamentRenderer::SyncSkybox(filament::Engine* engine,
                                  const std::string& path, float intensity) {
  skybox_path_ = path;
  skybox_synced_ = true;
  env_intensity_ = intensity;
  scene_revision_++;  // the probe's sky and first-bounce light change

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

  const bool is_hdr = path.size() >= 4 && [&] {
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".hdr";
  }();

  uint32_t image_width = 0;
  uint32_t image_height = 0;
  filament::Texture* equirect = nullptr;

  if (is_hdr) {
    // Radiance HDR panoramas carry values above 1, which is what the IBL
    // prefilter wants: the studio lights keep their intensity and show up in
    // reflections. Upload them as floats instead of 8-bit sRGB.
    auto bytes = os::ReadBytes(path);
    if (!bytes || bytes->empty() || bytes->size() > INT32_MAX) {
      PDebug::warn("Filament: could not read skybox panorama {}", path);
      return;
    }
    int width = 0, height = 0, channels = 0;
    float* pixels =
        stbi_loadf_from_memory(bytes->data(), static_cast<int>(bytes->size()),
                               &width, &height, &channels, 3);
    if (!pixels || width <= 0 || height <= 0) {
      if (pixels) stbi_image_free(pixels);
      PDebug::warn("Filament: could not decode skybox panorama {}", path);
      return;
    }
    image_width = static_cast<uint32_t>(width);
    image_height = static_cast<uint32_t>(height);
    const uint8_t levels = std::max<uint8_t>(
        1, static_cast<uint8_t>(std::ilogbf(static_cast<float>(width)) + 1));
    equirect = filament::Texture::Builder()
                   .width(image_width)
                   .height(image_height)
                   .levels(levels)
                   .usage(filament::Texture::Usage::SAMPLEABLE |
                          filament::Texture::Usage::UPLOADABLE |
                          filament::Texture::Usage::GEN_MIPMAPPABLE)
                   .format(filament::Texture::InternalFormat::R11F_G11F_B10F)
                   .sampler(filament::Texture::Sampler::SAMPLER_2D)
                   .build(*engine);
    if (!equirect) {
      stbi_image_free(pixels);
      return;
    }
    const size_t bytes_float =
        size_t{image_width} * image_height * 3 * sizeof(float);
    equirect->setImage(
        *engine, 0,
        filament::Texture::PixelBufferDescriptor(
            pixels, bytes_float, filament::backend::PixelDataFormat::RGB,
            filament::backend::PixelDataType::FLOAT, 1, 0, 0, 0,
            [](void* buffer, size_t, void*) { stbi_image_free(buffer); }));
  } else {
    std::shared_ptr<ImageData> image = ImageData::Load(path);
    if (!image || image->pixels.empty() || image->width <= 0 ||
        image->height <= 0) {
      PDebug::warn("Filament: could not load skybox panorama {}", path);
      return;
    }
    if (image->width != 2 * image->height) {
      PDebug::warn(
          "Filament: skybox panorama {} ({}x{}) is not 2:1 equirectangular",
          path, image->width, image->height);
      return;
    }
    image_width = static_cast<uint32_t>(image->width);
    image_height = static_cast<uint32_t>(image->height);
    const uint8_t levels = std::max<uint8_t>(
        1, static_cast<uint8_t>(std::ilogbf(static_cast<float>(image->width)) +
                                1));
    equirect = filament::Texture::Builder()
                   .width(image_width)
                   .height(image_height)
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
  }

  if (image_width != 2 * image_height) {
    PDebug::warn(
        "Filament: skybox panorama {} ({}x{}) is not 2:1 equirectangular", path,
        image_width, image_height);
    garbage_textures_.emplace_back(frame_, equirect);
    return;
  }

  Prefilter& filters = EnsurePrefilter(engine);
  sky_cube_ = filters.to_cube(equirect);
  garbage_textures_.emplace_back(frame_, equirect);
  if (!sky_cube_) return;

  ibl_reflections_ = filters.specular(sky_cube_);
  IBLPrefilterContext::IrradianceFilter::Options irr_options;
  irr_options.generateMipmap = false;
  ibl_irradiance_ = filters.irradiance(irr_options, sky_cube_);

  panorama_sky_ = filament::Skybox::Builder()
                      .environment(sky_cube_)
                      .intensity(intensity)
                      .build(*engine);
  ambient_ = filament::IndirectLight::Builder()
                 .reflections(ibl_reflections_)
                 .irradiance(ibl_irradiance_)
                 .intensity(intensity)
                 .build(*engine);
  if (scene_) {
    scene_->setSkybox(panorama_sky_);
    scene_->setIndirectLight(ambient_);
  }
  has_panorama_ibl_ = true;
}

void FilamentRenderer::SyncProbe(filament::Engine* engine, GameScene& game) {
  const ProbeVolumeData& probe = game.GetLighting().probes;
  const bool wanted = probe.placed && probe.enabled;
  if (!wanted) {
    if (probe_wanted_ || probe_light_) {
      probe_wanted_ = false;
      RetireProbeLight();
      probe_revision_ = 0;
      probe_capture_frame_ = 0;
      // The probe no longer overrides the scene's own image-based light.
      if (scene_) scene_->setIndirectLight(ambient_);
    }
    return;
  }
  probe_wanted_ = true;

  // The probe moving or being retuned is as good as the scene changing.
  if (probe.min != probe_values_.min || probe.max != probe_values_.max ||
      probe.intensity != probe_values_.intensity ||
      probe.bounces != probe_values_.bounces ||
      probe.capture_size != probe_values_.capture_size) {
    probe_values_ = probe;
    scene_revision_++;
  }
  if (scene_revision_ != probe_seen_revision_) {
    probe_seen_revision_ = scene_revision_;
    probe_change_frame_ = frame_;
  }

  // Capture once the scene has settled (a load adds meshes over several
  // frames), and not again until it has been still for a moment.
  const bool stale = !probe_light_ || probe_revision_ != scene_revision_;
  const bool settled = frame_ >= probe_change_frame_ + kProbeQuietFrames;
  // The first capture goes as soon as the scene has settled; later ones (and
  // retries after a failed allocation) wait out the minimum interval.
  const bool rested = probe_capture_frame_ == 0 ||
                      frame_ >= probe_capture_frame_ + kProbeMinInterval;
  if (stale && settled && rested) {
    const uint64_t revision = scene_revision_;
    probe_capture_frame_ = frame_;
    // Only a capture that happened counts as seeing this revision.
    if (CaptureProbe(engine, probe, game.GetRenderSettings().soft_shadows)) {
      probe_revision_ = revision;
    }
  }

  // The probe's light replaces the skybox's (or the ambient hemisphere's).
  if (probe_light_ && scene_ && scene_->getIndirectLight() != probe_light_) {
    scene_->setIndirectLight(probe_light_);
  }
}

bool FilamentRenderer::EnsureProbeTargets(filament::Engine* engine,
                                          uint32_t size) {
  if (probe_cube_ && probe_size_ == size) return true;
  DestroyProbeTargets(engine);

  // The prefilter wants every mip level of a power-of-two cubemap.
  const uint8_t levels = static_cast<uint8_t>(std::bit_width(size));
  probe_cube_ = filament::Texture::Builder()
                    .width(size)
                    .height(size)
                    .levels(levels)
                    .usage(filament::Texture::Usage::COLOR_ATTACHMENT |
                           filament::Texture::Usage::SAMPLEABLE |
                           filament::Texture::Usage::GEN_MIPMAPPABLE)
                    .format(filament::Texture::InternalFormat::RGBA16F)
                    .sampler(filament::Texture::Sampler::SAMPLER_CUBEMAP)
                    .build(*engine);
  const auto depth_format =
      filament::Texture::isTextureFormatSupported(
          *engine, filament::Texture::InternalFormat::DEPTH32F)
          ? filament::Texture::InternalFormat::DEPTH32F
          : filament::Texture::InternalFormat::DEPTH24;
  probe_depth_ = filament::Texture::Builder()
                     .width(size)
                     .height(size)
                     .levels(1)
                     .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
                     .format(depth_format)
                     .build(*engine);
  if (!probe_cube_ || !probe_depth_) {
    DestroyProbeTargets(engine);
    return false;
  }
  for (int face = 0; face < 6; face++) {
    probe_faces_[face] =
        filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR0,
                     probe_cube_)
            .face(filament::RenderTarget::AttachmentPoint::COLOR0,
                  static_cast<filament::RenderTarget::CubemapFace>(face))
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH,
                     probe_depth_)
            .build(*engine);
    if (!probe_faces_[face]) {
      DestroyProbeTargets(engine);
      return false;
    }
  }
  probe_size_ = size;

  if (!probe_renderer_) {
    probe_renderer_ = engine->createRenderer();
    // Anything the sky does not cover is black, not left over.
    filament::Renderer::ClearOptions clear;
    clear.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    clear.clear = true;
    probe_renderer_->setClearOptions(clear);
  }
  if (!probe_view_) {
    probe_camera_entity_ = utils::EntityManager::get().create();
    probe_camera_ = engine->createCamera(probe_camera_entity_);
    probe_view_ = engine->createView();
    probe_view_->setScene(scene_);
    probe_view_->setCamera(probe_camera_);
    // The raw lit radiance, before any tone mapping or grading: the filters
    // and the IndirectLight work in linear HDR.
    probe_view_->setPostProcessingEnabled(false);
    // Every layer: the probe sees the whole room whatever the main camera's
    // culling mask is.
    probe_view_->setVisibleLayers(0xff, 0xff);
    probe_view_->setAntiAliasing(filament::View::AntiAliasing::NONE);
    probe_view_->setDithering(filament::View::Dithering::NONE);
    probe_view_->setBlendMode(filament::View::BlendMode::OPAQUE);
  }
  probe_view_->setViewport(filament::Viewport(0, 0, size, size));
  return true;
}

void FilamentRenderer::DestroyProbeTargets(filament::Engine* engine) {
  // The six views may still be in flight.
  if (probe_cube_ || probe_view_) engine->flushAndWait();
  if (probe_view_) {
    engine->destroy(probe_view_);
    engine->destroyCameraComponent(probe_camera_entity_);
    utils::EntityManager::get().destroy(probe_camera_entity_);
    probe_view_ = nullptr;
    probe_camera_ = nullptr;
    probe_camera_entity_ = {};
  }
  if (probe_renderer_) {
    engine->destroy(probe_renderer_);
    probe_renderer_ = nullptr;
  }
  for (filament::RenderTarget*& face : probe_faces_) {
    if (face) engine->destroy(face);
    face = nullptr;
  }
  if (probe_depth_) {
    engine->destroy(probe_depth_);
    probe_depth_ = nullptr;
  }
  if (probe_cube_) {
    engine->destroy(probe_cube_);
    probe_cube_ = nullptr;
  }
  probe_size_ = 0;
}

void FilamentRenderer::RetireProbeLight() {
  if (probe_light_) {
    if (scene_ && scene_->getIndirectLight() == probe_light_) {
      scene_->setIndirectLight(ambient_);
    }
    garbage_indirect_lights_.emplace_back(frame_, probe_light_);
    probe_light_ = nullptr;
  }
  if (probe_reflections_) {
    garbage_textures_.emplace_back(frame_, probe_reflections_);
    probe_reflections_ = nullptr;
  }
  if (probe_irradiance_) {
    garbage_textures_.emplace_back(frame_, probe_irradiance_);
    probe_irradiance_ = nullptr;
  }
}

bool FilamentRenderer::CaptureProbe(filament::Engine* engine,
                                    const ProbeVolumeData& probe,
                                    bool soft_shadows) {
  const uint32_t size = std::bit_floor(
      static_cast<uint32_t>(std::clamp(probe.capture_size, 64, 1024)));
  if (!EnsureProbeTargets(engine, size)) {
    PDebug::warn("Filament: could not allocate the {}px light probe", size);
    return false;
  }

  // Start again from the scene's own light, not the previous capture's, so a
  // recapture does not pile bounces on top of old bounces.
  RetireProbeLight();
  filament::Skybox* sky_before = scene_->getSkybox();
  scene_->setSkybox(panorama_sky_ ? panorama_sky_ : fallback_sky_);
  scene_->setIndirectLight(ambient_);
  if (fallback_sky_) fallback_sky_->setColor(probe_background_);

  // With include_emission off the room is captured without emissive
  // surfaces' own glow (an AreaLight stands for it); put it back after.
  auto set_emission = [&](bool on) {
    for (auto& [entity, record] : proxies_) {
      for (DrawRecord& draw : record.draws) {
        if (!draw.material || glm::length(draw.emission) <= 0.0f) continue;
        draw.material->setParameter(
            "emissive",
            ToFilament(glm::vec4(on ? draw.emission : glm::vec3(0.0f), 1.0f)));
      }
    }
  };
  if (!probe.include_emission) set_emission(false);

  probe_view_->setShadowType(soft_shadows ? filament::ShadowType::PCSS
                                          : filament::ShadowType::PCF);
  probe_camera_->setProjection(90.0, 1.0, 0.02, 200.0);

  // The faces are the usual cubemap ones: +X, -X, +Y, -Y, +Z, -Z, each seen
  // from the probe with these up vectors (the ones OpenGL and Vulkan cube
  // sampling expect).
  struct Face {
    filament::math::double3 forward;
    filament::math::double3 up;
  };
  const Face faces[6] = {
      {{1, 0, 0}, {0, -1, 0}}, {{-1, 0, 0}, {0, -1, 0}},
      {{0, 1, 0}, {0, 0, 1}},  {{0, -1, 0}, {0, 0, -1}},
      {{0, 0, 1}, {0, -1, 0}}, {{0, 0, -1}, {0, -1, 0}},
  };
  const glm::vec3 center = (probe.min + probe.max) * 0.5f;
  const filament::math::double3 eye{center.x, center.y, center.z};

  Prefilter& filters = EnsurePrefilter(engine);
  IBLPrefilterContext::IrradianceFilter::Options irradiance_options;
  irradiance_options.generateMipmap = false;  // the specular pass makes them

  // The captured pixels are exposed (radiance times the camera's exposure);
  // the IndirectLight multiplies them by its intensity and the viewing
  // camera's exposure, so cancel the capturing camera's. The probe camera
  // keeps Filament's default exposure, the same as the scene cameras (the
  // exposure control is in the colour grading), so emissive surfaces read the
  // same in the capture as on screen.
  const float exposure =
      std::max(filament::Exposure::exposure(*probe_camera_), 1e-9f);

  const int bounces = std::clamp(probe.bounces, 1, 4);
  for (int bounce = 0; bounce < bounces; bounce++) {
    for (int face = 0; face < 6; face++) {
      probe_camera_->lookAt(eye, eye + faces[face].forward, faces[face].up);
      probe_view_->setRenderTarget(probe_faces_[face]);
      probe_renderer_->renderStandaloneView(probe_view_);
    }
    filament::Texture* reflections = filters.specular(probe_cube_);
    filament::Texture* irradiance_map =
        filters.irradiance(irradiance_options, probe_cube_);
    // The user's intensity scales only the light that ends up on screen; a
    // bounce lights the next one at face value or the factor would compound.
    const bool last = bounce == bounces - 1;
    const float intensity =
        (last ? std::max(probe.intensity, 0.0f) : 1.0f) / exposure;
    filament::IndirectLight* light = filament::IndirectLight::Builder()
                                         .reflections(reflections)
                                         .irradiance(irradiance_map)
                                         .intensity(intensity)
                                         .build(*engine);
    // The next bounce is lit by this one; the one before it is done with.
    scene_->setIndirectLight(light);
    RetireProbeLight();
    probe_light_ = light;
    probe_reflections_ = reflections;
    probe_irradiance_ = irradiance_map;
  }
  probe_view_->setRenderTarget(nullptr);
  if (!probe.include_emission) set_emission(true);
  scene_->setSkybox(sky_before);
  probe_captures_++;
  return true;
}

void FilamentRenderer::SetProbeBackground(const glm::vec4& srgb) {
  const glm::vec4 clamped = glm::clamp(srgb, 0.0f, 1.0f);
  const filament::math::float4 linear =
      ToFilament(glm::vec4(SrgbToLinear(glm::vec3(clamped)), 1.0f));
  if (linear.x != probe_background_.x || linear.y != probe_background_.y ||
      linear.z != probe_background_.z) {
    probe_background_ = linear;
    scene_revision_++;
  }
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
  for (utils::Entity area : areas_) {
    if (scene_) scene_->remove(area);
    engine->destroy(area);
  }
  areas_.clear();
  area_values_.clear();
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
  env_intensity_ = 0.0f;
  ambient_sky_ = glm::vec3(-1.0f);
  ambient_ground_ = glm::vec3(-1.0f);
}

}  // namespace Mercury::fil
