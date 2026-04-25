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

void PongScene::StartNewRound() {
  PDebug::log("Starting new round...");
  auto transform = ball_->Get<Transform>();
  constexpr int degree_range = 150;
  float degree = rand() % degree_range;
  float offset = degree_range + ((180 - degree_range) / 2.0f);
  degree += offset;
  auto new_vel = glm::vec3(glm::sin(glm::radians(degree)),
                           glm::cos(glm::radians(degree)), 0.0f);
  constexpr float SPEED = 40.0;
  transform->SetPosition(glm::vec3(0.0f));
  transform->SetVelocity(new_vel * SPEED);
}

void PongScene::OnStart() {
  srand(time(NULL));

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
    physics_engine_->Set<kMotionType>(player_, JPH::EMotionType::Kinematic);
    physics_engine_->Set<kRestitution>(player_, 0.0f);
  }
  {  // Create enemy
    enemy_ = object_manager_->NewObject<Box2D>(offset, glm::vec2(2.0f, 10.0f),
                                               glm::vec4(1.0f));
    physics_engine_->RegisterBody(enemy_, Physics::Box(player_dim),
                                  JPH::EAllowedDOFs::TranslationY);

    physics_engine_->Set<kGravity>(enemy_, 0.0f);
    physics_engine_->Set<kMotionType>(enemy_, JPH::EMotionType::Kinematic);
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

    // auto transform = ball_->Get<Transform>();
    // transform->SetVelocity(glm::vec3(-1.0f * 50, 0.0f, 0.0f));
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
        glm::vec2((-screen.x / 2.0f) - (dim.x / 2.0f), 0), dim,
        glm::vec4(1.0f));
    physics_engine_->RegisterBody(wall, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kMotionType>(wall->id, JPH::EMotionType::Kinematic);
    this->left_wall_ = wall->id;
  }
  {  // Create right wall
    auto dim = glm::vec2(20, screen.y);
    auto wall = object_manager_->NewObject<Box2D>(
        glm::vec2((screen.x / 2.0f) + (dim.x / 2.0f), 0.0f), dim,
        glm::vec4(1.0f));
    physics_engine_->RegisterBody(wall, Physics::Box(dim),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->Set<kMotionType>(wall->id, JPH::EMotionType::Kinematic);
    this->right_wall_ = wall->id;
    physics_engine_->AddCollisionCallback<kCollisionEnter>(
        wall->id, [this](kEntityId self, kEntityId other) {
          PDebug::log("Right wall touched...");
          this->player_wins_ += 1;
          this->StartNewRound();
        });
  }

  StartNewRound();
};

void PongScene::OnFrame(double delta_t) {
  ImGui::ShowDemoWindow();

  {
    auto transform = ball_->Get<Transform>();
    auto pos = transform->GetPosition();
    auto vel = transform->GetVelocity();
    ImGui::Begin("Ball");
    ImGui::Text("POSITION X: %f, Y: %f", pos.x, pos.y);
    ImGui::Text("VELOCITY X: %f, Y: %f", vel.x, vel.y);
    ImGui::End();
  }

  {
    ImGui::Begin("Scores");
    ImGui::Text("Player: %d", this->player_wins_);
    ImGui::Text("Enemy: %d", this->enemy_wins_);
    ImGui::End();
  }
};

void PongScene::OnTick(double delta_t) {
  constexpr int PLAYER_SPEED = 80;
  if (input_manager_) {
    if (input_manager_->IsPressed(GLFW_KEY_ESCAPE)) {
      this->QuitScene();
    }

    float direction = 0.0f;
    if (input_manager_->IsPressed(GLFW_KEY_W)) {
      direction += PLAYER_SPEED * 0.05 * delta_t;
    }
    if (input_manager_->IsPressed(GLFW_KEY_S)) {
      direction += -PLAYER_SPEED * 0.05 * delta_t;
    }
    auto transform = player_->Get<Transform>();
    transform->SetVelocity(glm::vec3(0.0f, direction, 0.0f));
  }

  {  // Enemy AI
    auto ball_transform = ball_->Get<Transform>();
    auto enemy_transform = enemy_->Get<Transform>();
    auto ball_position = ball_transform->GetPosition();

    PDebug::log("BALL[{}]: {}", player_->id, ball_position.y);
    enemy_transform->SetPosition(
        glm::vec3(enemy_transform->GetPosition().x, ball_position.y, 0.0));
  }
};

void PongScene::OnDestroy() {}
void PongScene::DestroyBrick() { PDebug::warn("Brick has collided"); };