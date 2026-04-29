//
// Created by smadhur on 4/25/2026.
//

#include "breakout_scene.h"

#include "GLFW/glfw3.h"
#include "imgui.h"
#include "physics_engine/shapes/box.h"
#include "pobject/nodes/box2d.h"
#include "properties/transform.h"
#include <physics_engine/shapes/plane.h>
constexpr float ZOOM = 2.0f;
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))
constexpr float PLAYER_SPEED = 20;
constexpr float BALL_SPEED = 160;

void BreakoutScene::OnStart() {
  {  // Camera
    player_camera_->SetZoom(ZOOM);
  }
  auto offset = (glm::vec2(scaled_width, scaled_height) / 2.0f) - glm::vec2(20);
  {
    auto dim = glm::vec2(36, 6);
    player_ =
        object_manager_->NewObject<Box2D>(glm::vec2(0.0f, -offset.y), dim);

    physics_engine_->RegisterBody(player_, Physics::Box(dim),
                                  JPH::EAllowedDOFs::TranslationX);
    physics_engine_->Set<kMotionType>(*player_, JPH::EMotionType::Dynamic);
    physics_engine_->Set<kRestitution>(*player_, 0.0f);
    // physics_engine_->Set<kFriction>(*player_, 0.0f);
  }
  {  // left wall
    auto wall = object_manager_->NewObject<PObject>();
    wall->Add<Transform>(glm::vec3(-scaled_width / 2.0f, 0, 0));
    physics_engine_->RegisterBody(wall, Physics::Plane(glm::vec3(1, 0, 0)),
                                  JPH::EAllowedDOFs::All);
  }
  {  // right wall
    auto wall = object_manager_->NewObject<PObject>();
    wall->Add<Transform>(glm::vec3(scaled_width / 2.0f, 0, 0));
    physics_engine_->RegisterBody(wall, Physics::Plane(glm::vec3(-1, 0, 0)),
                                  JPH::EAllowedDOFs::All);
  }

  {  // bottom follow
    auto floor = object_manager_->NewObject<PObject>();
    floor->Add<Transform>(glm::vec3(0.0f, -scaled_height / 2.0f, 0));
    physics_engine_->RegisterBody(floor, Physics::Plane(glm::vec3(0, 1, 0)),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->AddCollisionCallback<kCollisionEnter>(
        floor->id, [](kEntityId self, kEntityId other) {
          PDebug::info("Ball has fallen off");
        });
  }
  {
    auto roof = object_manager_->NewObject<PObject>();
    roof->Add<Transform>(glm::vec3(0.0f, scaled_height / 2.0f, 0));
    physics_engine_->RegisterBody(roof, Physics::Plane(glm::vec3(0, -1, 0)),
                                  JPH::EAllowedDOFs::All);
  }

  {
    constexpr float ball_size = 8.0f;
    ball_ = object_manager_->NewObject<Box2D>(glm::vec2(0.0f),
                                              glm::vec2(ball_size));
    physics_engine_->RegisterBody(
        ball_, Physics::Box(glm::vec3(ball_size, ball_size, 1.0f)),
        JPH::EAllowedDOFs::Plane2D);
    physics_engine_->Set<kGravity>(*ball_, 0.0f);
    physics_engine_->Set<kRestitution>(*ball_, 1.0f);
    // physics_engine_->Set<kFriction>(*ball_, 0.0f);
    physics_engine_->Set<kMotionType>(*ball_, JPH::EMotionType::Dynamic);
    auto transform = ball_->Get<Transform>();
    float degree = 88;
    auto direction = glm::vec3(glm::cos(glm::radians(degree)),
                               glm::sin(glm::radians(degree)), 0.0f);
    transform->SetVelocity(direction * BALL_SPEED);
    physics_engine_->AddCollisionCallback<kCollisionLeave>(
        ball_->id, [this](kEntityId self, kEntityId other) {
          {
            auto transform = this->ball_->Get<Transform>();
            auto vel = transform->GetVelocity();
            vel = glm::normalize(vel) * BALL_SPEED;
            transform->SetVelocity(vel);
          }
        });
  }

  {
    glm::vec2 brick_size = glm::vec2(32, 8);
    glm::vec2 position = glm::vec2(-offset.x, offset.y);
    for (int i = 0; i < 10; i++) {
      position.y = offset.y;
      for (int j = 0; j < 5; j++) {
        auto brick = object_manager_->NewObject<Box2D>(position, brick_size);
        physics_engine_->RegisterBody(brick, Physics::Box(brick_size),
                                      JPH::EAllowedDOFs::Plane2D);
        physics_engine_->Set<kMotionType>(*brick, JPH::EMotionType::Static);

        position.y += brick_size.y - 32;
      }
      position.x += brick_size.x + 32;
    }
  }
}

void BreakoutScene::OnDestroy() {}
void BreakoutScene::OnFrame(double delta_t) {
  {
    auto transform = ball_->Get<Transform>();
    auto vel = transform->GetVelocity();
    ImGui::Begin("Ball");
    ImGui::Text("VELOCITY: %f, %f", vel.x, vel.y);
    auto magnitude = glm::length(vel);
    ImGui::Text("MAGNITUDE: %f", magnitude);
    ImGui::End();
  }
}
void BreakoutScene::OnTick(double delta_t) {
  float direction = 0.0f;
  if (input_manager_->IsPressed(GLFW_KEY_A) ||
      input_manager_->IsPressed(GLFW_KEY_LEFT)) {
    direction -= 1.0f;
  }
  if (input_manager_->IsPressed(GLFW_KEY_D) ||
      input_manager_->IsPressed(GLFW_KEY_RIGHT)) {
    direction += 1.0f;
  }

  auto transform = player_->Get<Transform>();
  transform->SetVelocity(
      glm::vec3(direction * PLAYER_SPEED * delta_t, 0.0f, 0.0f));

  if (input_manager_->IsJustPressed(GLFW_KEY_ESCAPE)) {
    this->QuitScene();
  }
}
