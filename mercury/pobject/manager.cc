// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "manager.h"

namespace Mercury {

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

kEntityId PObjectManager::NewObject() { return registry_.create(); }

void PObjectManager::DestroyObject(kEntityId id) {
  if (registry_.valid(id)) registry_.destroy(id);
}

}  // namespace Mercury
