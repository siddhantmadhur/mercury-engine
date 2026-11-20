// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A scene-wide subsystem: one instance per scene, running after the physics
// step. It uses the scene's EventBus instead of calling other game code
// directly, the pattern a network or replication system would use.

#include <debugger/debugger.h>
#include <scene/map_scene.h>
#include <scripting/event_bus.h>
#include <scripting/system.h>

namespace {

struct MapReady {
  int map_number = 0;
};

class HelloSystem : public Mercury::System {
 public:
  // kPostPhysics is the slot a networking system uses to send after the
  // world has stepped; kPreTick is its receive counterpart.
  Phase GetPhase() const override { return Phase::kPostPhysics; }

  void OnRegister(Mercury::MapScene& scene) override {
    events_ =
        scene.GetEventBus().Subscribe<MapReady>([](const MapReady& event) {
          PDebug::info("Hello system (post-physics): map {} is ready",
                       event.map_number);
        });
  }

  void OnMapLoaded(Mercury::MapScene& scene) override {
    scene.GetEventBus().Publish(MapReady{++maps_loaded_});
  }

 private:
  Mercury::EventBus::Subscription events_;
  int maps_loaded_ = 0;
};

}  // namespace

MERCURY_SYSTEM(HelloSystem);
