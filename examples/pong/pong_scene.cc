//
// Created by smadhur on 4/20/2026.
//

#include "pong_scene.h"

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "imgui/imgui.h"
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
  {  // Create player
    player_ =
        object_manager_->NewObject<Box2D>(-offset, player_dim, glm::vec4(1.0f));

    physics_engine_->RegisterBody(player_, Physics::Box(player_dim),
                                  JPH::EAllowedDOFs::TranslationY);

    physics_engine_->Set<kGravity>(player_, 0.0f);
    physics_engine_->Set<kMotionType>(player_, JPH::EMotionType::Dynamic);
    physics_engine_->Set<kRestitution>(player_, 0.0f);
  }
  {  // Create enemy
    enemy_ = object_manager_->NewObject<Box2D>(offset, glm::vec2(2.0f, 10.0f),
                                               glm::vec4(1.0f));
    physics_engine_->RegisterBody(enemy_, Physics::Box(player_dim),
                                  JPH::EAllowedDOFs::TranslationY);

    physics_engine_->Set<kGravity>(enemy_, 0.0f);
    physics_engine_->Set<kMotionType>(enemy_, JPH::EMotionType::Dynamic);
    physics_engine_->Set<kRestitution>(enemy_, 0.0f);
  }
  {  // Create ball
    auto dim = glm::vec2(2.0f);
    ball_ = object_manager_->NewObject<Box2D>(glm::vec2(0.0f), dim,
                                              glm::vec4(1.0f));
    physics_engine_->RegisterBody(
        ball_, Physics::Box(dim),
        JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY);

    physics_engine_->Set<kMotionType>(ball_, JPH::EMotionType::Dynamic);
    physics_engine_->Set<kRestitution>(ball_, 1.0f);
    physics_engine_->Set<kGravity>(ball_, 0.0f);

    auto transform = ball_->Get<Transform>();
    transform->SetVelocity(glm::vec3(-1.0f * 50, 0.0f, 0.0f));
  }
  auto screen = glm::vec2(scaled_width, scaled_height);
  {  // Create top roof
    auto dim = glm::vec2(screen.x, 20);
    auto roof = object_manager_->NewObject<Box2D>(
        glm::vec2(0, (screen.y / 2.0f) + 10), dim, glm::vec4(1.0f));
    physics_engine_->RegisterBody(roof, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kGravity>(roof->id, 0.0f);
    physics_engine_->Set<kMotionType>(roof->id, JPH::EMotionType::Kinematic);
  }
  {  // Create bottom floor
    auto dim = glm::vec2(screen.x, 20);
    auto roof = object_manager_->NewObject<Box2D>(
        glm::vec2(0, -(screen.y / 2.0f) - 10), dim, glm::vec4(1.0f));
    physics_engine_->RegisterBody(roof, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kGravity>(roof->id, 0.0f);
    physics_engine_->Set<kMotionType>(roof->id, JPH::EMotionType::Kinematic);
  }

  {  // Create left wall
    auto dim = glm::vec2(20, screen.y);
    auto wall = object_manager_->NewObject<Box2D>(
        glm::vec2((-screen.x / 2.0f) - (dim.x / 2.0f)), dim, glm::vec4(1.0f));
    physics_engine_->RegisterBody(wall, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kMotionType>(wall->id, JPH::EMotionType::Kinematic);
  }
  {  // Create right wall
    auto dim = glm::vec2(20, screen.y);
    auto wall = object_manager_->NewObject<Box2D>(
        glm::vec2((screen.x / 2.0f) + (dim.x / 2.0f)), dim, glm::vec4(1.0f));
    physics_engine_->RegisterBody(wall, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kMotionType>(wall->id, JPH::EMotionType::Kinematic);
  }
};

void PongScene::OnFrame(double delta_t) {
  ImGui::ShowDemoWindow();

  {
    auto transform = ball_->Get<Transform>();
    auto vel = transform->GetVelocity();
    ImGui::Begin("Ball");
    ImGui::Text("X: %f, Y: %f", vel.x, vel.y);
    ImGui::End();
  }
};

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