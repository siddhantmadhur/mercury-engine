//
// Created by smadhur on 4/20/2026.
//

#ifndef PEQUODENGINE_PONG_SCENE_H
#define PEQUODENGINE_PONG_SCENE_H

#include "../common/example_utils.h"
#include "scene/scene.h"

using namespace Pequod;

class PongScene : public GameScene {
 public:
  void OnStart() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;
  void OnDestroy() override;

 protected:
  void StartNewRound();
  // Drives a kinematic paddle vertically, stopping it at the top and bottom
  // edges (kinematic bodies do not collide with the static walls).
  void MovePaddle(kEntityId paddle, float velocity_y);

  int player_wins_ = 0;
  int enemy_wins_ = 0;

  kEntityId left_wall_ = entt::null;
  kEntityId right_wall_ = entt::null;

 private:
  kEntityId player_ = entt::null;
  kEntityId enemy_ = entt::null;
  kEntityId ball_ = entt::null;
  float paddle_limit_ = 0.0f;  // max |y| of a paddle's center
  Examples::CollisionQueue goals_;
};

#endif  // PEQUODENGINE_PONG_SCENE_H
