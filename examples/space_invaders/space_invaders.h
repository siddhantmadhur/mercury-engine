//
// Created by smadhur on 4/25/2026.
//

#ifndef PEQUODENGINE_SPACE_INVADERS_H
#define PEQUODENGINE_SPACE_INVADERS_H
#include "scene/scene.h"

using namespace Pequod;

class SpaceInvaders : public GameScene {
 public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

 protected:
  std::shared_ptr<PObject> player_ = nullptr;
};

#endif  // PEQUODENGINE_SPACE_INVADERS_H
