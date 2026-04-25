//
// Created by smadhur on 4/25/2026.
//

#ifndef PEQUODENGINE_BREAKOUT_SCENE_H
#define PEQUODENGINE_BREAKOUT_SCENE_H
#include "scene/scene.h"

using namespace Pequod;

class BreakoutScene : public GameScene {
 public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

 protected:
  kEntityId bricks_[5][10] = {};
  kEntityId player_ = -1;
};
#endif  // PEQUODENGINE_BREAKOUT_SCENE_H
