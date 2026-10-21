//
// Created by smadhur on 4/25/2026.
//

#include "breakout_scene.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "GLFW/glfw3.h"
#include "debugger/debugger.h"
#include "imgui.h"
#include "properties/transform.h"

constexpr float ZOOM = 2.0f;           // screen pixels per world unit
constexpr float PLAYER_SPEED = 260.0f;  // world units per second
constexpr float BALL_SPEED = 160.0f;    // world units per second
constexpr float MIN_VERTICAL = 0.35f;   // of the ball's direction
constexpr float BALL_SIZE = 8.0f;
constexpr float BODY_DEPTH = 10.0f;
const glm::vec2 PLAYER_DIM = glm::vec2(36, 6);
constexpr glm::vec2 BRICK_DIM = glm::vec2(32, 8);

void BreakoutScene::OnStart() {
  srand(time(NULL));

  const glm::vec2 screen = glm::vec2(GetWidth(), GetHeight()) / ZOOM;
  Examples::SetupCamera2D(*object_manager_, player_camera_, screen.y);
  auto offset = (screen / 2.0f) - glm::vec2(20);

  {  // Paddle
    player_y_ = -offset.y;
    player_limit_ = screen.x / 2.0f - PLAYER_DIM.x / 2.0f;
    player_ = object_manager_->NewBox2D(glm::vec2(0.0f, player_y_),
                                        PLAYER_DIM, glm::vec4(1.0f));
    physics_engine_->CreateBox3D(
        player_, glm::vec3(PLAYER_DIM, BODY_DEPTH),
        [](JPH::BodyCreationSettings& settings) {
          Examples::Configure2DBody(settings, JPH::EMotionType::Kinematic);
          settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX;
        });
  }

  // Walls are static half-spaces whose normals point into the playfield.
  auto new_wall = [this](glm::vec3 position, glm::vec3 normal) {
    auto wall = object_manager_->NewObject();
    object_manager_->AddProperty(wall, Transform(position));
    physics_engine_->CreatePlane(wall, normal);
    return wall;
  };
  new_wall(glm::vec3(-screen.x / 2.0f, 0, 0), glm::vec3(1, 0, 0));
  new_wall(glm::vec3(screen.x / 2.0f, 0, 0), glm::vec3(-1, 0, 0));
  new_wall(glm::vec3(0.0f, screen.y / 2.0f, 0), glm::vec3(0, -1, 0));
  floor_ = new_wall(glm::vec3(0.0f, -screen.y / 2.0f, 0), glm::vec3(0, 1, 0));

  // Collision callbacks run on Jolt worker threads, so they only record the
  // hit; OnTick acts on it.
  auto record = [this](kEntityId self, kEntityId other) {
    collisions_.Push(self, other);
  };
  physics_engine_->AddCollisionCallback<kCollisionEnter>(floor_, record);

  {  // Ball
    ball_ = object_manager_->NewBox2D(glm::vec2(0.0f), glm::vec2(BALL_SIZE),
                                      glm::vec4(1.0f));
    physics_engine_->CreateBox3D(
        ball_, glm::vec3(BALL_SIZE, BALL_SIZE, BODY_DEPTH),
        [](JPH::BodyCreationSettings& settings) {
          Examples::Configure2DBody(settings, JPH::EMotionType::Dynamic);
          settings.mRestitution = 1.0f;
        });
    ResetBall();
  }

  {  // Bricks: 10 columns x 5 rows, centered horizontally
    constexpr float column_step = BRICK_DIM.x + 32;
    glm::vec2 position = glm::vec2(-column_step * 4.5f, offset.y);
    for (int i = 0; i < 10; i++) {
      position.y = offset.y;
      for (int j = 0; j < 5; j++) {
        auto brick = object_manager_->NewBox2D(
            position, BRICK_DIM,
            glm::vec4(0.4f + 0.12f * j, 0.8f - 0.12f * j, 0.9f, 1.0f));
        physics_engine_->CreateBox3D(
            brick, glm::vec3(BRICK_DIM, BODY_DEPTH),
            [](JPH::BodyCreationSettings& settings) {
              settings.mMotionType = JPH::EMotionType::Static;
              settings.mRestitution = 1.0f;
              settings.mFriction = 0.0f;
            });
        physics_engine_->AddCollisionCallback<kCollisionEnter>(brick, record);
        bricks_.insert(brick);

        position.y -= BRICK_DIM.y + 16;
      }
      position.x += column_step;
    }
  }
}

void BreakoutScene::ResetBall() {
  // Launch upwards from just above the paddle, 20-40 degrees off vertical.
  float degree = 90.0f + ((rand() % 2 == 0) ? 1.0f : -1.0f) *
                             static_cast<float>(20 + rand() % 21);
  auto direction = glm::vec3(glm::cos(glm::radians(degree)),
                             glm::sin(glm::radians(degree)), 0.0f);
  auto transform = object_manager_->GetProperty<Transform>(ball_);
  transform->SetPosition(glm::vec3(0.0f, player_y_ + 24.0f, 0.0f));
  transform->SetVelocity(direction * BALL_SPEED);
}

void BreakoutScene::OnDestroy() {}

void BreakoutScene::OnFrame(double delta_t) {
  auto transform = object_manager_->GetProperty<Transform>(ball_);
  auto vel = transform->GetVelocity();
  ImGui::SetNextWindowPos(ImVec2(10, GetHeight() - 10), ImGuiCond_FirstUseEver,
                          ImVec2(0, 1));
  ImGui::Begin("Ball", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("VELOCITY: %f, %f", vel.x, vel.y);
  ImGui::Text("MAGNITUDE: %f", glm::length(vel));
  ImGui::Separator();
  ImGui::Text("Bricks left: %d", static_cast<int>(bricks_.size()));
  ImGui::Text("Balls lost: %d", balls_lost_);
  if (bricks_.empty()) ImGui::Text("You win!");
  ImGui::End();
}

void BreakoutScene::OnTick(double delta_t) {
  if (input_manager_->IsJustPressed(GLFW_KEY_ESCAPE)) {
    this->QuitScene();
  }

  bool ball_lost = false;
  for (auto [self, other] : collisions_.Drain()) {
    if (other != ball_) continue;
    if (self == floor_) {
      ball_lost = true;
    } else if (bricks_.erase(self) > 0) {
      Examples::DestroyEntity(*physics_engine_, *object_manager_, self);
    }
  }
  if (ball_lost) {
    PDebug::info("Ball has fallen off");
    balls_lost_ += 1;
    ResetBall();
  }

  {  // Paddle: kinematic, so it is stopped at the walls here
    float direction = 0.0f;
    if (input_manager_->IsPressed(GLFW_KEY_A) ||
        input_manager_->IsPressed(GLFW_KEY_LEFT)) {
      direction -= 1.0f;
    }
    if (input_manager_->IsPressed(GLFW_KEY_D) ||
        input_manager_->IsPressed(GLFW_KEY_RIGHT)) {
      direction += 1.0f;
    }

    auto transform = object_manager_->GetProperty<Transform>(player_);
    auto pos = transform->GetPosition();
    if (std::abs(pos.x) > player_limit_) {
      pos.x = std::clamp(pos.x, -player_limit_, player_limit_);
      transform->SetPosition(pos);
    }
    if ((pos.x >= player_limit_ && direction > 0.0f) ||
        (pos.x <= -player_limit_ && direction < 0.0f)) {
      direction = 0.0f;
    }
    transform->SetVelocity(glm::vec3(direction * PLAYER_SPEED, 0.0f, 0.0f));
  }

  // Keep the ball at a constant speed, and never let it settle into a
  // near-horizontal path between the side walls. Only correct it when needed:
  // a velocity set this tick overrides that tick's bounces.
  if (!ball_lost) {
    auto transform = object_manager_->GetProperty<Transform>(ball_);
    auto vel = transform->GetVelocity();
    const float speed = glm::length(vel);
    if (speed > 0.0f) {
      auto dir = vel / speed;
      bool correct = std::abs(speed - BALL_SPEED) > 1.0f;
      if (std::abs(dir.y) < MIN_VERTICAL) {
        dir.y = std::copysign(MIN_VERTICAL, dir.y == 0.0f ? 1.0f : dir.y);
        dir.x = std::copysign(std::sqrt(1.0f - MIN_VERTICAL * MIN_VERTICAL),
                              dir.x);
        correct = true;
      }
      if (correct) transform->SetVelocity(dir * BALL_SPEED);
    }
  }
}
