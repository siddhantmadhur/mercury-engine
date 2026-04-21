//
// Created by smadhur on 4/20/2026.
//

#include "pong_scene.h"

#include "debugger/debugger.h"
#include "pobject/nodes/box2d.h"

void PongScene::OnStart() {
  player_camera_ = std::make_unique<Camera>(16.0 / 9.0);
  player_camera_->configure2D(1280, 720, 100.0f);

  auto player = object_manager_->NewObject<Box2D>(
      glm::vec2(1.0f), glm::vec2(5.0f), glm::vec4(1.0f));
};

void PongScene::OnFrame(double delta_t) {};

void PongScene::OnTick(double delta_t) {};

void PongScene::OnDestroy() {};