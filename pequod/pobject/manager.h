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

#include "globals.h"
#include "pobject.h"

namespace Pequod {
class PObjectManager {
 public:
  PObjectManager();

  std::vector<Primitive> GetPrimitives();

  template <std::derived_from<PObject> PType, class... Args>
    requires std::constructible_from<PType, Args...>
  std::shared_ptr<PObject> NewObject(Args &&...args) {
    std::shared_ptr<PType> object =
        std::make_shared<PType>(std::forward<Args>(args)...);
    object->id = objects.size();
    objects.push_back(object);
    return object;
  }

  void DeleteObject(std::shared_ptr<PObject>);

  std::shared_ptr<PObject> GetObjectById(entity_id id) {
    if (id < objects.size()) return objects[id];
    return nullptr;
  }

 private:
  std::vector<std::shared_ptr<PObject>> objects = {};
};
}  // namespace Pequod

#endif  // PEQUOD_POBJECT_MANAGER_H
