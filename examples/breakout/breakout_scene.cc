//
// Created by smadhur on 4/25/2026.
//

#include "breakout_scene.h"

#include "GLFW/glfw3.h"
#include "physics_engine/shapes/box.h"
#include "pobject/nodes/box2d.h"
#include "properties/transform.h"

constexpr float ZOOM = 2.0f;
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))

void BreakoutScene::OnStart() {
  {  // Camera
    player_camera_ = std::make_unique<Camera>(GetWidth() / GetHeight());
    player_camera_->configure2D(GetWidth(), GetHeight(), ZOOM);
  }
  auto offset = (glm::vec2(scaled_width, scaled_height) / 2.0f) - glm::vec2(20);
  {
    auto dim = glm::vec2(36, 6);
    auto player =
        object_manager_->NewObject<Box2D>(glm::vec2(0.0f, -offset.y), dim);
    player_ = player->id;

    physics_engine_->RegisterBody(player, Physics::Box(dim),
                                  JPH::EAllowedDOFs::TranslationX);
    physics_engine_->Set<kMotionType>(*player, JPH::EMotionType::Kinematic);
    physics_engine_->Set<kRestitution>(*player, 1.0f);
  }
}

void BreakoutScene::OnDestroy() {}
void BreakoutScene::OnFrame(double delta_t) {}
void BreakoutScene::OnTick(double delta_t) {
  constexpr int SPEED = 20;
  float direction = 0.0f;
  if (input_manager_->IsPressed(GLFW_KEY_A)) {
    direction -= 1.0f;
  }
  if (input_manager_->IsPressed(GLFW_KEY_D)) {
    direction += 1.0f;
  }

  auto player = object_manager_->GetObjectById(player_);
  auto transform = player->Get<Transform>();
  transform->SetVelocity(glm::vec3(direction * SPEED * delta_t, 0.0f, 0.0f));

  if (input_manager_->IsJustPressed(GLFW_KEY_ESCAPE)) {
    this->QuitScene();
  }
}
