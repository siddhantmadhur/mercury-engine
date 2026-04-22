//
// Created by smadhur on 4/20/2026.
//

#include "pong_scene.h"

#include "debugger/debugger.h"
#include "pobject/nodes/box2d.h"

#define ZOOM 12.0f
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))

void PongScene::OnStart() {
  player_camera_ = std::make_unique<Camera>(GetWidth() / GetHeight());
  player_camera_->configure2D(GetWidth(), GetHeight(), ZOOM);

  auto offset = glm::vec2((scaled_width / 2.0f) - 12.0f, 0.0f);
  player_ = object_manager_->NewObject<Box2D>(-offset, glm::vec2(2.0f, 10.0f),
                                              glm::vec4(1.0f));
  enemy_ = object_manager_->NewObject<Box2D>(offset, glm::vec2(2.0f, 10.0f),
                                             glm::vec4(1.0f));
};

void PongScene::OnFrame(double delta_t) {};

void PongScene::OnTick(double delta_t) {};

void PongScene::OnDestroy() {};