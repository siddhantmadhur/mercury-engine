// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * system.h
 *
 * Engine-level systems: C++ code that runs next to a MapScene instead of on a
 * single map node. A Behaviour is the gameplay building block (one instance per
 * component); a System is the subsystem building block (one instance per
 * scene), which is what whole features such as networking, replication or a
 * custom renderer layer need.
 *
 * Systems are registered with MERCURY_SYSTEM(Type), the same static-init
 * pattern MERCURY_SCRIPT uses. MapScene creates one instance of every
 * registered system when the scene starts, then calls the systems in phase and
 * order around the map's behaviours. See @ref scripting_guide.
 */
#ifndef MERCURY_SCRIPTING_SYSTEM_H
#define MERCURY_SCRIPTING_SYSTEM_H

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Mercury {

class MapScene;

/**
 * @brief A scene-wide subsystem driven by @ref MapScene
 *
 * A System receives the same frame and tick callbacks as a @ref Behaviour, but
 * once per scene rather than once per object, and it can reach the whole scene
 * through the `MapScene&` argument (registry, physics, assets, event bus,
 * renderer hooks). It is the extension point for features that are not a
 * component: networking, replay, streaming, custom editor-side simulation.
 *
 * Lifecycle:
 * 1. `OnRegister` once, when the scene starts.
 * 2. `OnMapLoaded` whenever a map is built (and `OnMapUnloading` before it is
 *    replaced).
 * 3. `OnPreFrame` / `OnFrame` / `OnLateFrame` every rendered frame, around the
 *    behaviours' `OnFrame` / `OnLateFrame`.
 * 4. `OnTick` every fixed tick, in the system's @ref Phase.
 * 5. `OnShutdown` once, when the scene is destroyed.
 *
 * Frame callbacks run for every system, ordered by phase then order; the phase
 * only changes where a system sits relative to the behaviours' tick and the
 * physics step.
 */
class System {
 public:
  /**
   * @brief Where a system runs inside a fixed tick
   *
   * `kPreTick` runs before collision dispatch and the behaviours (network
   * receive); `kTick` after the behaviours (simulation); `kPostTick` after the
   * script timers but before physics; `kPostPhysics` after the physics step
   * (network send).
   */
  enum class Phase {
    kPreTick = 0,
    kTick = 1,
    kPostTick = 2,
    kPostPhysics = 3,
  };

  virtual ~System() = default;

  // Called once when the scene starts, in phase/order. Register event
  // listeners or allocate scene-wide state here.
  virtual void OnRegister(MapScene& scene) {}
  // A map was built into the scene, including the first one. Fires again for
  // every LoadMap/Instantiate, without restarting the system.
  virtual void OnMapLoaded(MapScene& scene) {}
  // The current map is about to be destroyed (a map switch or ClearMap). The
  // entities are still valid.
  virtual void OnMapUnloading(MapScene& scene) {}
  // Every rendered frame, before the behaviours' OnFrame.
  virtual void OnPreFrame(MapScene& scene, double delta_time) {}
  // Every rendered frame, after the behaviours' OnFrame.
  virtual void OnFrame(MapScene& scene, double delta_time) {}
  // Every fixed tick. dt is in seconds (same as Behaviour::OnTick).
  virtual void OnTick(MapScene& scene, double delta_time) {}
  // Every rendered frame, after the behaviours' OnLateFrame.
  virtual void OnLateFrame(MapScene& scene, double delta_time) {}
  // Called once when the scene is destroyed. Entities are still valid.
  virtual void OnShutdown(MapScene& scene) {}

  // Which part of the tick the system belongs to. Override in the subclass.
  virtual Phase GetPhase() const { return Phase::kTick; }
  // Lower runs earlier inside a phase. Ties are broken by name.
  virtual int GetOrder() const { return 0; }
};

/**
 * @brief One registered System type, as stored by SystemRegistry
 */
struct SystemInfo {
  std::string name;
  System::Phase phase = System::Phase::kTick;
  int order = 0;
  std::function<std::unique_ptr<System>()> create;
};

/**
 * @brief The process-wide list of registered System types
 *
 * Registration is normally a static initializer (MERCURY_SYSTEM), so the list
 * is complete before any scene is created. Like TypeRegistry, the singleton is
 * intentionally process-wide: a game links its systems into its executable.
 */
class SystemRegistry {
 public:
  static SystemRegistry& Instance();

  // Registering the same name again replaces the earlier entry.
  void Register(SystemInfo info);
  // Sorted by phase, then order, then name.
  const std::vector<SystemInfo>& Systems() const { return systems_; }
  // Removes every registration (tests, and hosts that build a list by hand).
  void Clear();

 private:
  SystemRegistry() = default;

  std::vector<SystemInfo> systems_;
};

/**
 * @brief Register a C++ System subclass so MapScene runs it
 *
 * Use through MERCURY_SYSTEM(Type) in the system's .cc file. The type must
 * derive from @ref System and be default-constructible.
 *
 * @par Example
 * @code{.cpp}
 * class NetworkSystem : public Mercury::System {
 *  public:
 *   Phase GetPhase() const override { return Phase::kPreTick; }
 *   int GetOrder() const override { return -100; }
 *   void OnRegister(Mercury::MapScene& scene) override {
 *     voice_ = scene.GetEventBus().Subscribe<VoiceEvent>(
 *         [this](const VoiceEvent& event) { Queue(event); });
 *   }
 *   void OnTick(Mercury::MapScene& scene, double dt) override { Receive(); }
 *  private:
 *   Mercury::EventBus::Subscription voice_;
 * };
 *
 * MERCURY_SYSTEM(NetworkSystem);
 * @endcode
 *
 * @return true, so the macro can initialize a variable with it.
 */
template <typename T>
bool RegisterSystem(const char* name) {
  static_assert(std::is_base_of_v<System, T>,
                "MERCURY_SYSTEM types must derive from Mercury::System");
  static_assert(std::is_default_constructible_v<T>,
                "MERCURY_SYSTEM types must be default-constructible");
  T instance{};
  SystemInfo info;
  info.name = name;
  info.phase = instance.GetPhase();
  info.order = instance.GetOrder();
  info.create = [] { return std::unique_ptr<System>(new T()); };
  SystemRegistry::Instance().Register(std::move(info));
  return true;
}

}  // namespace Mercury

/**
 * @brief Register a C++ System so every MapScene runs it
 * @param Type A class deriving from Mercury::System
 *
 * Put this in the system's .cc file. It runs at static initialization time.
 */
#define MERCURY_SYSTEM(Type)                                    \
  [[maybe_unused]] static const bool mercury_system_##Type##_ = \
      ::Mercury::RegisterSystem<Type>(#Type)

#endif  // MERCURY_SCRIPTING_SYSTEM_H
