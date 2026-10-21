//
// Created by smadhur on 4/20/2026.
//

#include "pong_scene.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "imgui/imgui.h"
#include "properties/transform.h"

constexpr float ZOOM = 8.0f;  // screen pixels per world unit
constexpr float BALL_SPEED = 60.0f;     // world units per second
constexpr float PLAYER_SPEED = 70.0f;   // world units per second
constexpr float ENEMY_SPEED = 45.0f;    // slower than the ball, so it can lose
constexpr float MIN_HORIZONTAL = 0.45f;  // of the ball's direction
const glm::vec2 PADDLE_DIM = glm::vec2(2.0f, 10.0f);
const glm::vec2 BALL_DIM = glm::vec2(2.0f);
constexpr float BODY_DEPTH = 10.0f;

void PongScene::StartNewRound() {
  PDebug::log("Starting new round...");
  // Serve within 35 degrees of horizontal, towards a random side.
  float degree = static_cast<float>(rand() % 70) - 35.0f;
  float side = (rand() % 2 == 0) ? 1.0f : -1.0f;
  auto direction = glm::vec3(side * glm::cos(glm::radians(degree)),
                             glm::sin(glm::radians(degree)), 0.0f);

  auto transform = object_manager_->GetProperty<Transform>(ball_);
  transform->SetPosition(glm::vec3(0.0f));
  transform->SetVelocity(direction * BALL_SPEED);
}

void PongScene::OnStart() {
  srand(time(NULL));

  const glm::vec2 screen = glm::vec2(GetWidth(), GetHeight()) / ZOOM;
  Examples::SetupCamera2D(*object_manager_, player_camera_, screen.y);
  paddle_limit_ = screen.y / 2.0f - PADDLE_DIM.y / 2.0f;

  auto paddle_settings = [](JPH::BodyCreationSettings& settings) {
    Examples::Configure2DBody(settings, JPH::EMotionType::Kinematic);
    settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationY;
  };

  auto offset = glm::vec2((screen.x / 2.0f) - 12.0f, 0.0f);
  {  // Create player
    player_ = object_manager_->NewBox2D(-offset, PADDLE_DIM, glm::vec4(1.0f));
    physics_engine_->CreateBox3D(player_, glm::vec3(PADDLE_DIM, BODY_DEPTH),
                                 paddle_settings);
  }
  {  // Create enemy
    enemy_ = object_manager_->NewBox2D(offset, PADDLE_DIM, glm::vec4(1.0f));
    physics_engine_->CreateBox3D(enemy_, glm::vec3(PADDLE_DIM, BODY_DEPTH),
                                 paddle_settings);
  }
  {  // Create ball
    ball_ = object_manager_->NewBox2D(glm::vec2(0.0f), BALL_DIM,
                                      glm::vec4(1.0f));
    physics_engine_->CreateBox3D(
        ball_, glm::vec3(BALL_DIM, BODY_DEPTH),
        [](JPH::BodyCreationSettings& settings) {
          Examples::Configure2DBody(settings, JPH::EMotionType::Dynamic);
          settings.mRestitution = 1.0f;
        });
  }

  // Walls are static half-spaces whose normals point into the playfield.
  auto new_wall = [this](glm::vec3 position, glm::vec3 normal) {
    auto wall = object_manager_->NewObject();
    object_manager_->AddProperty(wall, Transform(position));
    physics_engine_->CreatePlane(wall, normal);
    return wall;
  };
  new_wall(glm::vec3(0.0f, screen.y / 2.0f, 0.0f), glm::vec3(0, -1, 0));
  new_wall(glm::vec3(0.0f, -screen.y / 2.0f, 0.0f), glm::vec3(0, 1, 0));
  left_wall_ =
      new_wall(glm::vec3(-screen.x / 2.0f, 0.0f, 0.0f), glm::vec3(1, 0, 0));
  right_wall_ =
      new_wall(glm::vec3(screen.x / 2.0f, 0.0f, 0.0f), glm::vec3(-1, 0, 0));

  // Reaching a side wall scores a point. The callback runs on a Jolt worker
  // thread, so it only records the hit; OnTick acts on it.
  for (auto wall : {left_wall_, right_wall_}) {
    physics_engine_->AddCollisionCallback<kCollisionEnter>(
        wall, [this](kEntityId self, kEntityId other) {
          goals_.Push(self, other);
        });
  }

  StartNewRound();
}

void PongScene::OnFrame(double delta_t) {
  {
    auto transform = object_manager_->GetProperty<Transform>(ball_);
    auto pos = transform->GetPosition();
    auto vel = transform->GetVelocity();
    ImGui::SetNextWindowPos(ImVec2(10, 70), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ball");
    ImGui::Text("POSITION X: %f, Y: %f", pos.x, pos.y);
    ImGui::Text("VELOCITY X: %f, Y: %f", vel.x, vel.y);
    ImGui::End();
  }

  {
    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scores");
    ImGui::Text("Player: %d", this->player_wins_);
    ImGui::Text("Enemy: %d", this->enemy_wins_);
    ImGui::End();
  }
}

void PongScene::MovePaddle(kEntityId paddle, float velocity_y) {
  auto transform = object_manager_->GetProperty<Transform>(paddle);
  auto pos = transform->GetPosition();
  if (pos.y > paddle_limit_ || pos.y < -paddle_limit_) {
    pos.y = std::clamp(pos.y, -paddle_limit_, paddle_limit_);
    transform->SetPosition(pos);
  }
  if ((pos.y >= paddle_limit_ && velocity_y > 0.0f) ||
      (pos.y <= -paddle_limit_ && velocity_y < 0.0f)) {
    velocity_y = 0.0f;
  }
  transform->SetVelocity(glm::vec3(0.0f, velocity_y, 0.0f));
}

void PongScene::OnTick(double delta_t) {
  if (input_manager_ && input_manager_->IsPressed(GLFW_KEY_ESCAPE)) {
    this->QuitScene();
  }

  bool scored = false;
  for (auto [wall, other] : goals_.Drain()) {
    if (other != ball_ || scored) continue;
    if (wall == right_wall_) {
      PDebug::log("Player scored");
      player_wins_ += 1;
    } else {
      PDebug::log("Enemy scored");
      enemy_wins_ += 1;
    }
    scored = true;
  }
  if (scored) StartNewRound();

  {  // Player
    float direction = 0.0f;
    if (input_manager_) {
      if (input_manager_->IsPressed(GLFW_KEY_W)) direction += 1.0f;
      if (input_manager_->IsPressed(GLFW_KEY_S)) direction -= 1.0f;
    }
    MovePaddle(player_, direction * PLAYER_SPEED);
  }

  auto ball_transform = object_manager_->GetProperty<Transform>(ball_);
  {  // Enemy AI: chase the ball, capped at ENEMY_SPEED
    auto enemy_y = object_manager_->GetProperty<Transform>(enemy_)
                       ->GetPosition()
                       .y;
    float diff = ball_transform->GetPosition().y - enemy_y;
    MovePaddle(enemy_, std::clamp(diff * 8.0f, -ENEMY_SPEED, ENEMY_SPEED));
  }

  // Keep the ball at a constant speed: moving paddles add energy on contact,
  // and a near-vertical ball would never reach either side. Only correct it
  // when needed: a velocity set this tick overrides that tick's bounces.
  if (!scored) {
    auto vel = ball_transform->GetVelocity();
    const float speed = glm::length(vel);
    if (speed > 0.0f) {
      auto dir = vel / speed;
      bool correct = std::abs(speed - BALL_SPEED) > 1.0f;
      if (std::abs(dir.x) < MIN_HORIZONTAL) {
        dir.x = std::copysign(MIN_HORIZONTAL, dir.x == 0.0f ? 1.0f : dir.x);
        dir.y = std::copysign(
            std::sqrt(1.0f - MIN_HORIZONTAL * MIN_HORIZONTAL), dir.y);
        correct = true;
      }
      if (correct) ball_transform->SetVelocity(dir * BALL_SPEED);
    }
  }
}

void PongScene::OnDestroy() {}
