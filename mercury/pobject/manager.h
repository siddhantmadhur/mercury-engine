// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * manager.h
 *
 * Manager to handle PObjects. Lets you create new PObjects and free them.
 *
 */
#ifndef MERCURY_POBJECT_MANAGER_H
#define MERCURY_POBJECT_MANAGER_H
#define ENTT_USE_STL 1
#include <entt/entt.hpp>

#include "globals.h"
#include "properties/camera.h"
#include "properties/transform.h"

namespace Mercury {

/**
 * @brief Manager to handle creating and deleting PObjects
 *
 * Usual use-case is to have one per scene and is used for the renderer and
 * application to understand what to draw.
 */
class PObjectManager {
 public:
  PObjectManager() = default;

  kEntityId NewObject();

  void ProcessOnFrame(float delta);
  void CaptureTickSnapshots();

  template <class T, typename... Args>
    requires std::constructible_from<T, Args...>
  void AddProperty(kEntityId self, Args&&... args) {
    registry_.emplace_or_replace<T>(self, std::forward<Args>(args)...);
  }

  template <class T>
  void AddProperty(kEntityId self, T property) {
    registry_.emplace_or_replace<T>(self, property);
  }

  // nullptr if the entity lacks T, or is null or destroyed.
  template <typename T>
  T* GetProperty(kEntityId self) {
    if (!registry_.valid(self)) return nullptr;
    return registry_.try_get<T>(self);
  }

  template <class T>
  void DeleteProperty(kEntityId self) {
    registry_.remove<T>(self);
  }

  entt::registry& GetRegistry() { return registry_; }
  const entt::registry& GetRegistry() const { return registry_; }

  // Removes the entity and every property it has.
  void DestroyObject(kEntityId id);

 private:
  entt::registry registry_;
};
}  // namespace Mercury

#endif  // MERCURY_POBJECT_MANAGER_H
