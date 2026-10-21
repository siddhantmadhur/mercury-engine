//
// Created by smadhur on 4/25/2026.
//

#include "space_invaders.h"

#include <filesystem>

#include "GLFW/glfw3.h"
#include "imgui.h"
#include "os/filesystem.h"
#include "properties/texture2d.h"
#include "properties/transform.h"

constexpr float PELLET_SPEED = 140.0f;  // world units per second
constexpr float PLAYER_SPEED = 90.0f;   // world units per second
constexpr float BODY_DEPTH = 10.0f;

void SpaceInvaders::OnStart() {
  const glm::vec2 screen = glm::vec2(GetWidth(), GetHeight()) / float(ZOOM);
  Examples::SetupCamera2D(*object_manager_, player_camera_, screen.y);
  glm::vec2 offset = (screen / 2.0f) - glm::vec2(24);

  {
    constexpr float player_size = 16.0f;
    player_ = object_manager_->NewBox2D(glm::vec2(0, -offset.y),
                                        glm::vec2(player_size),
                                        glm::vec4(1.0f));
    auto texture_path =
        std::filesystem::path(MERCURY_ASSET_PATH) / "space_invaders/player.png";
    object_manager_->AddProperty(player_, Texture2D(texture_path.string()));
    physics_engine_->CreateBox3D(
        player_, glm::vec3(glm::vec2(player_size), BODY_DEPTH),
        [](JPH::BodyCreationSettings& settings) {
          Examples::Configure2DBody(settings, JPH::EMotionType::Dynamic);
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
  new_wall(glm::vec3(-screen.x / 2.0f, 0.0f, 0), glm::vec3(1, 0, 0));
  new_wall(glm::vec3(screen.x / 2.0f, 0.0f, 0), glm::vec3(-1, 0, 0));
  roof_ = new_wall(glm::vec3(0.0f, screen.y / 2.0f, 0.0f), glm::vec3(0, -1, 0));

  // Collision callbacks run on Jolt worker threads, so they only record the
  // hit; OnTick acts on it.
  auto record = [this](kEntityId self, kEntityId other) {
    collisions_.Push(self, other);
  };
  physics_engine_->AddCollisionCallback<kCollisionEnter>(roof_, record);

  {  // Enemies: 5 rows of 8, centered horizontally
    auto texture_path =
        std::filesystem::path(MERCURY_ASSET_PATH) / "space_invaders/enemy1.png";
    auto size = glm::vec2(16.0f);
    const float column_step = size.x + 4;
    auto pos = glm::vec2(column_step * 3.5f, offset.y);
    for (int i = 0; i < 5; i++) {
      pos.x = column_step * 3.5f;
      for (int j = 0; j < 8; j++) {
        auto enemy = object_manager_->NewBox2D(pos, size, glm::vec4(1.0f));
        object_manager_->AddProperty(enemy, Texture2D(texture_path.string()));
        physics_engine_->CreateBox3D(
            enemy, glm::vec3(size - glm::vec2(2.0f), BODY_DEPTH),
            [](JPH::BodyCreationSettings& settings) {
              Examples::Configure2DBody(settings,
                                        JPH::EMotionType::Kinematic);
            });
        physics_engine_->AddCollisionCallback<kCollisionEnter>(enemy, record);
        enemies_.insert(enemy);

        pos.x -= column_step;
      }
      pos.y -= size.y + 4;
    }
  }
}

void SpaceInvaders::OnDestroy() {}

void SpaceInvaders::OnFrame(double delta_t) {
  if (input_manager_->IsPressed(GLFW_KEY_ESCAPE)) {
    QuitScene();
  }

  ImGui::Begin("Space Invaders");
  ImGui::Text("Score: %d", score_);
  ImGui::Text("Enemies left: %d", static_cast<int>(enemies_.size()));
  if (enemies_.empty()) ImGui::Text("You win!");
  ImGui::End();
}

void SpaceInvaders::OnTick(double delta_t) {
  for (auto [self, other] : collisions_.Drain()) {
    // Only pellets that are still alive count; a destroyed pellet can report
    // one more contact before its body is removed.
    if (!pellets_.contains(other)) continue;
    if (self == roof_) {
      pellets_.erase(other);
      Examples::DestroyEntity(*physics_engine_, *object_manager_, other);
    } else if (enemies_.erase(self) > 0) {
      pellets_.erase(other);
      Examples::DestroyEntity(*physics_engine_, *object_manager_, other);
      Examples::DestroyEntity(*physics_engine_, *object_manager_, self);
      score_ += 10;
    }
  }

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
  transform->SetVelocity(glm::vec3(direction * PLAYER_SPEED, 0.0f, 0.0f));

  if (input_manager_->IsJustPressed(GLFW_KEY_SPACE)) {
    ShootPellet();
  }
}

void SpaceInvaders::ShootPellet() {
  auto transform = object_manager_->GetProperty<Transform>(player_);
  auto pos = transform->GetPosition();
  pos.y += 12;

  auto pellet_size = glm::vec2(1, 6);
  auto pellet = object_manager_->NewBox2D(glm::vec2(pos), pellet_size,
                                          glm::vec4(1.0f));
  physics_engine_->CreateBox3D(
      pellet, glm::vec3(pellet_size, BODY_DEPTH),
      [](JPH::BodyCreationSettings& settings) {
        Examples::Configure2DBody(settings, JPH::EMotionType::Dynamic);
        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationY;
      });
  object_manager_->GetProperty<Transform>(pellet)->SetVelocity(
      glm::vec3(0, PELLET_SPEED, 0));
  pellets_.insert(pellet);
}
