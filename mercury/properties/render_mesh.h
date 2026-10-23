/*
 * render_mesh.h
 *
 * What the model renderer draws for an entity: shared geometry, one entry
 * per material, posed by a model's node hierarchy when there is one.
 * MeshRenderer, SkinnedMeshRenderer and TextMesh build these; the graphics
 * backends only read them.
 */
#ifndef MERCURY_PROPERTIES_RENDER_MESH_H
#define MERCURY_PROPERTIES_RENDER_MESH_H

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "globals.h"
#include "properties/model.h"

namespace Mercury {

struct RenderMaterial {
  glm::vec4 color = glm::vec4(1.0f);  // sRGB with alpha, as in Unity
  std::shared_ptr<ImageData> texture;
  // Draw a camera's RenderTexture instead of `texture` (res:// path of the
  // RenderTexture asset).
  std::string render_texture;
  glm::vec2 tiling = glm::vec2(1.0f);
  glm::vec2 offset = glm::vec2(0.0f);
  float metallic = 0.0f;
  float smoothness = 0.5f;
  glm::vec3 emission = glm::vec3(0.0f);  // linear, added to the lit color
  bool transparent = false;
  bool double_sided = false;
  bool unlit = false;
  // The texture is a signed-distance font atlas (TextMesh).
  bool sdf_text = false;
};

struct RenderDraw {
  std::shared_ptr<const MeshGeometry> geometry;
  int material = 0;
  // Model node that places the draw; -1 uses `local` instead.
  int node = -1;
  glm::mat4 local = glm::mat4(1.0f);  // relative to the entity
  int skin = -1;                      // index into RenderMesh::skins
};

struct RenderSkin {
  std::vector<ModelBone> bones;
};

/**
 * @brief Node matrices of an animated model, written by the Animator
 */
struct ModelPose {
  std::vector<glm::mat4> locals;
  std::vector<glm::mat4> globals;  // model space
  bool valid = false;
};

struct RenderMesh {
  std::vector<RenderDraw> draws;
  std::vector<RenderMaterial> materials;
  std::shared_ptr<const Model> model;  // set when draws use model nodes
  // Draws are placed relative to this model node (the one the entity
  // stands for); -1 places them in model space.
  int base_node = -1;
  std::vector<RenderSkin> skins;
  bool visible = true;
  bool cast_shadows = true;
  // Unity's renderingLayerMask; bit 8 ("Outline") draws an outline.
  uint32_t rendering_layers = 1;
  // Entity whose ModelPose animates this mesh (the Animator's), or null.
  kEntityId pose_entity = entt::null;
  glm::vec3 bounds_min = glm::vec3(0.0f);  // entity space
  glm::vec3 bounds_max = glm::vec3(0.0f);

  // Where `draw` sits relative to the entity, with the model posed by `pose`
  // (the bind pose when null or invalid).
  glm::mat4 DrawMatrix(const RenderDraw& draw, const ModelPose* pose) const;
  // Bone matrices (entity space) for a skinned draw.
  void SkinMatrices(const RenderDraw& draw, const ModelPose* pose,
                    std::vector<glm::mat4>& out) const;
  // Recomputes bounds_min/max from the draws in the bind pose.
  void ComputeBounds();
};

}  // namespace Mercury

#endif  // MERCURY_PROPERTIES_RENDER_MESH_H
