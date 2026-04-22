//
// Created by smadhur on 4/20/2026.
//

#ifndef PEQUODENGINE_PONG_SCENE_H
#define PEQUODENGINE_PONG_SCENE_H
#include <input/input_manager.h>

#include "scene/scene.h"

using namespace Pequod;

class PongScene : public GameScene {
 public:
  void OnStart() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;
  void OnDestroy() override;

  void DestroyBrick();

 private:
  std::shared_ptr<PObject> player_ = nullptr;
  std::shared_ptr<PObject> enemy_ = nullptr;
  std::map<kEntityId, std::function<void()>> on_collisions_ = {};
};

#endif  // PEQUODENGINE_PONG_SCENE_H
