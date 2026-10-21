//
// Created by smadhur on 4/25/2026.
//

#ifndef MERCURYENGINE_SPACE_INVADERS_H
#define MERCURYENGINE_SPACE_INVADERS_H

#include <unordered_set>

#include "../common/example_utils.h"
#include "scene/scene.h"

constexpr int ZOOM = 4;  // screen pixels per world unit

using namespace Mercury;

class SpaceInvaders : public GameScene {
 public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

  void ShootPellet();

 protected:
  kEntityId player_ = entt::null;
  kEntityId roof_ = entt::null;
  std::unordered_set<kEntityId> enemies_;
  std::unordered_set<kEntityId> pellets_;
  int score_ = 0;
  Examples::CollisionQueue collisions_;
};

#endif  // MERCURYENGINE_SPACE_INVADERS_H
