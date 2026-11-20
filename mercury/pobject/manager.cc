// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "manager.h"

#include <atomic>

namespace Mercury {

namespace {

// Ids are unique for the life of the process, across every scene and every
// PObjectManager. Map entities override theirs with the MapNode uid in
// MapScene::CreateNodeEntity.
uint64_t NextGuid() {
  static std::atomic<uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

PObjectManager::PObjectManager() {
  // Keep the guid index correct even when an entity is destroyed through the
  // registry directly instead of DestroyObject.
  registry_.on_destroy<EntityGuid>().connect<&PObjectManager::OnGuidDestroyed>(
      this);
}

PObjectManager::~PObjectManager() {
  registry_.on_destroy<EntityGuid>().disconnect(this);
}

void PObjectManager::OnGuidDestroyed(entt::registry& registry,
                                     kEntityId entity) {
  if (const EntityGuid* guid = registry.try_get<EntityGuid>(entity)) {
    guid_index_.erase(guid->value);
  }
}

void PObjectManager::ProcessOnFrame(float delta) {
  auto view = registry_.view<Transform>();
  view.each([delta](auto entity, Transform& transform) {
    transform.Interpolate(delta);
  });
}

void PObjectManager::CaptureTickSnapshots() {
  auto view = registry_.view<Transform>();
  view.each([](auto entity, Transform& transform) {
    transform.CaptureTickSnapshot();
  });
}

kEntityId PObjectManager::NewObject() {
  const kEntityId id = registry_.create();
  const uint64_t guid = NextGuid();
  registry_.emplace<EntityGuid>(id, EntityGuid{guid});
  guid_index_[guid] = id;
  return id;
}

uint64_t PObjectManager::GetGuid(kEntityId id) const {
  if (!registry_.valid(id)) return 0;
  const EntityGuid* guid = registry_.try_get<EntityGuid>(id);
  return guid ? guid->value : 0;
}

bool PObjectManager::SetGuid(kEntityId id, uint64_t guid) {
  if (!registry_.valid(id)) return false;
  if (guid != 0) {
    auto existing = guid_index_.find(guid);
    if (existing != guid_index_.end() && existing->second != id &&
        registry_.valid(existing->second)) {
      return false;
    }
  }
  if (const EntityGuid* current = registry_.try_get<EntityGuid>(id)) {
    guid_index_.erase(current->value);
  }
  registry_.emplace_or_replace<EntityGuid>(id, EntityGuid{guid});
  if (guid != 0) guid_index_[guid] = id;
  return true;
}

kEntityId PObjectManager::FindByGuid(uint64_t guid) const {
  if (guid == 0) return entt::null;
  auto it = guid_index_.find(guid);
  if (it == guid_index_.end()) return entt::null;
  return registry_.valid(it->second) ? it->second : entt::null;
}

void PObjectManager::DestroyObject(kEntityId id) {
  if (registry_.valid(id)) registry_.destroy(id);
}

}  // namespace Mercury
