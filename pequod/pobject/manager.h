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
#include <memory>
#include <optional>

#include "globals.h"
#include "pobject.h"
#include "texture_atlas.h"

namespace Pequod {

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
  std::vector<Primitive> GetPrimitives();

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

  /**
   * @brief Create a new object in the manager
   *
   * @tparam PType The base class the object needs to be generated from (eg.
   * Box2D or Capsule)
   * @tparam Args List of arguments required for the base class
   * @return A shared pointer pointing to the new object created
   */
  template <class PType, class... Args>
    requires std::derived_from<PType, PObject> &&
             std::constructible_from<PType, Args...>
  std::shared_ptr<PType> NewObject(Args&&... args) {
    std::shared_ptr<PType> object =
        std::make_shared<PType>(std::forward<Args>(args)...);
    object->id = objects.size();
    objects.push_back(object);
    return object;
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

  TextureAtlas& GetAtlas() { return atlas_; }

  std::shared_ptr<PObject> GetObjectById(entity_id id) {
    if (id < objects.size()) return objects[id];
    return nullptr;
  }

 private:
  std::vector<std::shared_ptr<PObject>> objects = {};
  std::vector<StaticVertex> static_vertices_ = {};
  std::vector<UINT> static_indices_ = {};
  TextureAtlas atlas_;
  std::vector<Vertex> vertices_;
};
}  // namespace Pequod

#endif  // PEQUOD_POBJECT_MANAGER_H
