//
// Created by smadhur on 4/20/2026.
//

#include "pong_scene.h"

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "physics_engine/shapes/box.h"
#include "pobject/nodes/box2d.h"
#include "properties/transform.h"

#define ZOOM 8.0f
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))

void PongScene::OnStart() {
  player_camera_ = std::make_unique<Camera>(GetWidth() / GetHeight());
  player_camera_->configure2D(GetWidth(), GetHeight(), ZOOM);

  auto offset = glm::vec2((scaled_width / 2.0f) - 12.0f, 0.0f);
  auto player_dim = glm::vec2(2.0f, 10.0f);
  player_ =
      object_manager_->NewObject<Box2D>(-offset, player_dim, glm::vec4(1.0f));
  enemy_ = object_manager_->NewObject<Box2D>(offset, glm::vec2(2.0f, 10.0f),
                                             glm::vec4(1.0f));

  physics_engine_->RegisterBody(player_, Physics::Box(player_dim),
                                JPH::EAllowedDOFs::TranslationY);

  physics_engine_->SetGravity(player_->id, 0.0f);
  physics_engine_->SetMotionType(player_->id, JPH::EMotionType::Dynamic);

  physics_engine_->AddCollisionCallback<kCollisionPersisted>(
      player_->id,
      [](kEntityId self, kEntityId other) { PDebug::log("Player collided!"); });
  {
    auto temp = object_manager_->NewObject<Box2D>(
        -offset + glm::vec2(0.0f, 20.0f), glm::vec2(20.0f), glm::vec4(1.0f));
    physics_engine_->RegisterBody(temp, Physics::Box(glm::vec2(20.0f)),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->SetGravity(temp->id, 0.0f);
    physics_engine_->SetMotionType(temp->id, JPH::EMotionType::Dynamic);
  }
};

void PongScene::OnFrame(double delta_t) {};

#define SPEED 80

void PongScene::OnTick(double delta_t) {
  if (input_manager_) {
    if (input_manager_->IsPressed(GLFW_KEY_ESCAPE)) {
      this->QuitScene();
    }

    float direction = 0.0f;
    if (input_manager_->IsPressed(GLFW_KEY_W)) {
      direction += SPEED * 0.05 * delta_t;
    }
    if (input_manager_->IsPressed(GLFW_KEY_S)) {
      direction += -SPEED * 0.05 * delta_t;
    }
    auto transform = player_->Get<Transform>();
    transform->SetVelocity(glm::vec3(0.0f, direction, 0.0f));
  }
};

void PongScene::OnDestroy() {}
void PongScene::DestroyBrick() { PDebug::warn("Brick has collided"); };