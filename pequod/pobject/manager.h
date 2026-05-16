/*
 * manager.h
 *
 * Manager to handle PObjects. Let's you create new PObjects, free them,
 * and represent them in a tree-like structure.
 * The idea was to have this be separate from ECS internal implementation.
 *
 */
#ifndef PEQUOD_POBJECT_MANAGER_H
#define PEQUOD_POBJECT_MANAGER_H
#include <array>
#include <memory>
#include <optional>

#include "globals.h"
#include "texture_atlas.h"
#include "properties/camera.h"
#include <properties/transform.h>
#include <properties/texture2d.h>
#include <properties/mesh.h>
#define ENTT_USE_STL 1
#include <entt/entt.hpp>

#include "algorithms/sparse_set.h"
namespace Pequod {
enum Node {
  kEmpty,
  kBox2D,
};

constexpr unsigned int kMaxEntities = 80960;
// TODO: Going to replace this with a sparsed set later
#define PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Type) std::optional<Type> *

/**
 * @brief Manager to handle creating and deleting PObjects
 * * Usual use-case is to have one per scene and is used for the renderer and
 * application to understand what to draw. It's not quite an ECS but the idea
 * was similar where it is just an easy way to access Objects and their
 * properties
 *
 * You can also use it to compose new PObjects based off created templates
 * Like creating a simple Box can be done with Box2D.
 *
 * It is abstracted because I intend to change the memory layout later on and
 * this provides an easy way to do so without having user implementation change
 */
class PObjectManager {
 public:
  PObjectManager();

  /**
   * @brief Returns a list of primitives to draw
   *
   * Primitive are the basic building block that instructs the Application on
   * what to render. This function goes through every object it is managing and
   * creates a list of primitives for the renderer to draw. Any optimization
   * like batching vertices can be done here as well.
   *
   */
  std::vector<Primitive> GetPrimitives(bool refresh_vertices = true);

  /**
   * @brief Combines a group of primitives so it returns as one instance to the
   * GPU
   *
   * @param primary The main primitive for which the others will override their
   * Position(), Texture() values with
   * @param begin The first primitive to begin grouping with
   * @param end The last primitive to end grouping with
   */
  void GroupPrimitives(kEntityId primary, kEntityId begin, kEntityId end);

  static void UpdateAtlas(entt::registry &, const entt::entity);

  kEntityId NewObject();

  kEntityId NewBox2D(glm::vec2, glm::vec2, glm::vec4);

  kEntityId NewPlane2D(glm::vec3 position, glm::vec2 size,
                       glm::vec4 color = glm::vec4(1.0));

  kEntityId NewObjectFromFile(const std::string &file_path, float scale = 1.0,
                              glm::vec4 color = glm::vec4(1));

  kEntityId NewCube3D(glm::vec3 position = glm::vec3(0.0),
                      glm::vec3 size = glm::vec3(1.0),
                      glm::vec4 color = glm::vec4(1.0f));

  void ProcessOnFrame(float delta);
  void CaptureTickSnapshots();

  template <class T, typename... Args>
    requires std::constructible_from<T, Args...>
  void AddProperty(kEntityId self, Args &&...args) {
    registry_.emplace_or_replace<T>(self, std::forward<Args>(args)...);
  };

  template <class T>
  void AddProperty(kEntityId self, T property) {
    registry_.emplace_or_replace<T>(self, property);
  };

  template <typename T>
  T *GetProperty(kEntityId self) {
    auto property = registry_.try_get<T>(self);
    return property;
  }

  template <class T>
  void DeleteProperty(kEntityId self) {
    registry_.remove<T>(self);
  }

  /**
   * Moves object to a static buffer which means while its data cannot be
   * changed, it renders very well
   *
   * @param id Entity to make static
   */
  void MakeStatic(kEntityId id);

  std::vector<StaticVertex> GetStaticVertices() const;

  std::vector<UINT> GetStaticIndices() const;

  void GenerateVertices();

  std::vector<Vertex> GetVertices() const;

  void DeleteObject(kEntityId id);

  TextureAtlas &GetAtlas() { return atlas_; }

 private:
  struct StaticRange {
    size_t vertex_start;
    size_t vertex_count;
    Texture2D *tex;  // nullptr -> use atlas white pixel UV
  };

  void RefreshStaticAtlasUVs();

  std::vector<StaticVertex> static_vertices_ = {};
  std::vector<UINT> static_indices_ = {};
  std::vector<StaticRange> static_ranges_ = {};
  TextureAtlas atlas_;
  std::vector<Vertex> vertices_;

  /**
  std::tuple<PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Mesh),
             PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Transform),
             PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Camera),
             PEQUOD_MACRO_DO_NOT_USE_PROPERTY_LIST_TYPE(Texture2D)>
      properties_;
      **/

  entt::registry registry_;
};
}  // namespace Pequod

#endif  // PEQUOD_POBJECT_MANAGER_H
