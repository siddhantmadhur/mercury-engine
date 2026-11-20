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
#include <unordered_map>

#include "globals.h"
#include "properties/camera.h"
#include "properties/transform.h"

namespace Mercury {

/**
 * @brief A stable, process-unique id for an entity
 *
 * A map node's id is its MapNode uid, so it survives reloading the map; an
 * entity created at run time gets the next id from a counter. Systems use it
 * instead of an entt::entity when the id has to survive a scene rebuild or
 * travel over a network. 0 means "no id".
 */
struct EntityGuid {
  uint64_t value = 0;
};

/**
 * @brief Manager to handle creating and deleting PObjects
 *
 * Usual use-case is to have one per scene and is used for the renderer and
 * application to understand what to draw.
 */
class PObjectManager {
 public:
  PObjectManager();
  ~PObjectManager();
  PObjectManager(const PObjectManager&) = delete;
  PObjectManager& operator=(const PObjectManager&) = delete;

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

  // The entity's stable id, or 0 when it has none.
  uint64_t GetGuid(kEntityId id) const;
  // Overrides the id, e.g. with the MapNode uid of a map entity. Returns false
  // when `guid` (other than 0) already belongs to a different live entity, so
  // two entities can never share a lookup id. 0 clears the id.
  bool SetGuid(kEntityId id, uint64_t guid);
  // The entity with this id, or entt::null.
  kEntityId FindByGuid(uint64_t guid) const;

  // Removes the entity and every property it has.
  void DestroyObject(kEntityId id);

 private:
  void OnGuidDestroyed(entt::registry& registry, kEntityId entity);

  entt::registry registry_;
  // guid -> entity, kept in sync by NewObject, SetGuid and OnGuidDestroyed so
  // FindByGuid does not scan the registry.
  std::unordered_map<uint64_t, kEntityId> guid_index_;
};
}  // namespace Mercury

#endif  // MERCURY_POBJECT_MANAGER_H
