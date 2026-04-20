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

#include "ecs/ecs.hh"
#include "pobject.h"

namespace Pequod {
class PObjectManager {
 public:
  PObjectManager(std::shared_ptr<ECS>);

  template <std::derived_from<PObject> PType, class... Args>
  std::shared_ptr<PObject> NewObject(Args &&...args) {
    return NewObject<PType>(root, args...);
  }

  template <std::derived_from<PObject> PType, class... Args>
  std::shared_ptr<PObject> NewObject(std::shared_ptr<PObject> parent,
                                     Args &&...args) {
    std::shared_ptr<PObject> object = std::make_shared<PType>(args...);
    if (!ecs) {
      PDebug::error("ECS not set when creating new object.");
      return nullptr;
    }
    object->id = ecs->max_id++;
    object->SetECS(this->ecs);
    objects.push_back(object);

    if (parent == nullptr) {
      parent = root;  // parent can still be null if the object created was root
    }

    if (parent) {
      parents.push_back(parent);
      parent->children.push_back(object->id);
    }

    return object;
  }

  void DeleteObject(std::shared_ptr<PObject>);

  std::shared_ptr<PObject> GetRoot() { return root; }

  std::shared_ptr<PObject> GetObjectById(entity_id id) {
    if (id < objects.size()) return objects[id];
    return nullptr;
  }

 private:
  std::shared_ptr<PObject> root = nullptr;
  std::shared_ptr<ECS> ecs = nullptr;

  std::vector<std::shared_ptr<PObject> > objects = {};
  std::vector<std::shared_ptr<PObject> > parents =
      {};  // returns the parent of every node
};
}  // namespace Pequod

#endif  // PEQUOD_POBJECT_MANAGER_H
