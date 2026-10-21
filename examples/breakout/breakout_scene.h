//
// Created by smadhur on 4/25/2026.
//

#ifndef MERCURYENGINE_BREAKOUT_SCENE_H
#define MERCURYENGINE_BREAKOUT_SCENE_H

#include <unordered_set>

#include "../common/example_utils.h"
#include "scene/scene.h"

using namespace Mercury;

class BreakoutScene : public GameScene {
 public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

 protected:
  void ResetBall();

  std::unordered_set<kEntityId> bricks_;
  kEntityId player_ = entt::null;
  kEntityId ball_ = entt::null;
  kEntityId floor_ = entt::null;
  float player_limit_ = 0.0f;  // max |x| of the paddle's center
  float player_y_ = 0.0f;
  int balls_lost_ = 0;
  Examples::CollisionQueue collisions_;
};
#endif  // MERCURYENGINE_BREAKOUT_SCENE_H
