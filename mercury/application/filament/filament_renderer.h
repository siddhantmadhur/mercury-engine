// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * filament_renderer.h
 *
 * Bridges Mercury's world to Filament: RenderMesh entities become Filament
 * renderables (with skinned bones), ImageData becomes Filament textures, and
 * SceneLighting becomes Filament lights plus a constant ambient
 * IndirectLight. The caller owns the Engine and the Views; this class owns
 * the Scene and everything in it.
 */
#ifndef MERCURY_ENGINE_FILAMENT_FILAMENT_RENDERER_H
#define MERCURY_ENGINE_FILAMENT_FILAMENT_RENDERER_H

#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <utils/Entity.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "globals.h"
#include "properties/render_mesh.h"
#include "scene/scene.h"

namespace Mercury::fil {

class FilamentRenderer {
 public:
  FilamentRenderer() = default;
  ~FilamentRenderer() { /* Shutdown() must have been called */ }

  bool Init(filament::Engine* engine);
  void Shutdown(filament::Engine* engine);

  filament::Scene* scene() const { return scene_; }
  void SetFallbackSkybox(filament::Skybox* skybox);
  // Draws the panorama or the solid clear color. The IBL is the same either
  // way, so a camera can reflect the environment without showing it.
  void SetSkyboxForClear(bool show_panorama);
  // Registers offscreen RenderTexture color attachments keyed by res:// path.
  void SetRenderTextures(
      const std::unordered_map<std::string, filament::Texture*>& textures);

  // Brings the Filament world in step with the game's registry and lighting:
  // creates/removes renderables, updates transforms, bones and visibility,
  // and rebuilds the lights when the scene's lighting values change.
  void SyncScene(GameScene& game);

 private:
  struct MeshEntry {
    filament::VertexBuffer* vertices = nullptr;
    filament::IndexBuffer* indices = nullptr;
    filament::Box bounds;
    std::weak_ptr<const MeshGeometry> source;
  };
  struct TextureEntry {
    filament::Texture* texture = nullptr;
    std::weak_ptr<const ImageData> source;
  };
  struct DrawRecord {
    utils::Entity renderable;
    filament::MaterialInstance* material = nullptr;
    uint32_t layer = 0;
    uint32_t draw_index = 0;  // index into RenderMesh::draws
    bool skinned = false;
    bool in_scene = false;
    std::string render_texture;
  };
  struct ProxyRecord {
    std::vector<DrawRecord> draws;
    const RenderMesh* mesh = nullptr;
    glm::mat4 world = glm::mat4(1.0f);
    bool visible = false;
    uint64_t frame = 0;
  };

  filament::Material* LoadMaterial(const char* name);
  filament::VertexBuffer* UploadGeometry(filament::Engine* engine,
                                         const MeshGeometry& geometry,
                                         filament::Box* bounds);
  filament::Texture* UploadTexture(filament::Engine* engine,
                                   const std::shared_ptr<ImageData>& image);
  // Destroys this entity's renderables (and their material instances).
  void DestroyProxy(filament::Engine* engine, entt::entity entity);
  // (Re)builds every draw of the entity from its RenderMesh.
  void BuildProxy(filament::Engine* engine, GameScene& game,
                  entt::entity entity, const RenderMesh& mesh,
                  const Transform& transform, ProxyRecord& record);
  // Recreates the sun, point lights, skybox and IBL when values changed.
  void SyncLights(filament::Engine* engine, GameScene& game);
  void SyncSkybox(filament::Engine* engine, const std::string& path,
                  float intensity);
  void SweepGarbage(filament::Engine* engine, bool all);
  void DestroyLights(filament::Engine* engine);

  filament::Engine* engine_ = nullptr;
  filament::Scene* scene_ = nullptr;

  // [0=opaque, 1=transparent, 2=transmissive][double_sided]
  filament::Material* lit_[3][2] = {};
  filament::Material* unlit_ = nullptr;
  filament::Material* unlit_transparent_ = nullptr;
  filament::Material* sdf_ = nullptr;
  filament::Texture* white_ = nullptr;        // 1x1 white fallback
  filament::Texture* flat_normal_ = nullptr;  // 1x1 (0,0,1) normal fallback

  std::unordered_map<uint64_t, MeshEntry> meshes_;
  std::unordered_map<uint64_t, TextureEntry> textures_;
  std::unordered_map<std::string, filament::Texture*> render_textures_;
  std::unordered_map<entt::entity, ProxyRecord> proxies_;

  utils::Entity sun_ = {};
  std::vector<utils::Entity> points_;
  std::vector<utils::Entity> areas_;
  filament::IndirectLight* ambient_ = nullptr;
  filament::Skybox* fallback_sky_ = nullptr;
  filament::Skybox* panorama_sky_ = nullptr;
  filament::Texture* sky_cube_ = nullptr;
  filament::Texture* ibl_reflections_ = nullptr;
  filament::Texture* ibl_irradiance_ = nullptr;
  std::string skybox_path_;
  bool skybox_synced_ = false;
  bool has_panorama_ibl_ = false;
  float env_intensity_ = 0.0f;
  std::vector<std::pair<uint64_t, filament::IndirectLight*>>
      garbage_indirect_lights_;
  std::vector<std::pair<uint64_t, filament::Skybox*>> garbage_skyboxes_;
  std::vector<std::pair<uint64_t, filament::Texture*>> garbage_textures_;
  // What the current lights were built from, to notice changes.
  glm::vec3 sun_direction_ = glm::vec3(0.0f);
  glm::vec3 sun_color_ = glm::vec3(0.0f);
  bool sun_shadows_ = false;
  float shadow_distance_ = 0.0f;
  float shadow_bias_ = 0.0f;
  float shadow_normal_bias_ = 0.0f;
  std::vector<PointLightData> point_values_;
  std::vector<AreaLightData> area_values_;
  glm::vec3 ambient_sky_ = glm::vec3(-1.0f);
  glm::vec3 ambient_ground_ = glm::vec3(-1.0f);

  uint64_t frame_ = 0;
};

}  // namespace Mercury::fil

#endif  // MERCURY_ENGINE_FILAMENT_FILAMENT_RENDERER_H
