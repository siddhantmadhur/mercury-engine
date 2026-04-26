//
// Created by smadhur on 4/25/2026.
//

#include "space_invaders.h"

#include "GLFW/glfw3.h"
#include "os/filesystem.h"
#include "pobject/nodes/box2d.h"
#include "properties/texture2d.h"
#include "properties/transform.h"

#include <filesystem>

constexpr float PELLET_SPEED = 140.0f;
constexpr float PLAYER_SPEED = 20.0f;
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))

void SpaceInvaders::OnStart() {
  {  // Camera
    player_camera_ = std::make_unique<Camera>(GetWidth() / GetHeight());
    player_camera_->configure2D(GetWidth(), GetHeight(), ZOOM);
  }
  glm::vec2 offset =
      (glm::vec2(scaled_width, scaled_height) / 2.0f) - glm::vec2(24);
  {
    constexpr float player_size = 16.0f;
    player_ = object_manager_->NewObject<Box2D>(glm::vec2(0, -offset.y),
                                                glm::vec2(player_size));
    auto texture_path =
        std::filesystem::path(PEQUOD_ASSET_PATH) / "space_invaders/player.png";
    player_->Add<Texture2D>(texture_path.string());
    physics_engine_->RegisterBody(player_, Physics::Box(glm::vec2(player_size)),
                                  JPH::EAllowedDOFs::TranslationX);
    physics_engine_->Set<kGravity>(*player_, 0.0f);
    physics_engine_->Set<kFriction>(*player_, 0.0f);
    physics_engine_->Set<kMotionType>(*player_, JPH::EMotionType::Dynamic);
  }

  {
    auto wall = object_manager_->NewObject<PObject>();
    wall->Add<Transform>(glm::vec3(-scaled_width / 2.0f, 0.0f, 0));
    physics_engine_->RegisterBody(wall, Physics::Plane(glm::vec3(1, 0, 0)),
                                  JPH::EAllowedDOFs::All);
  }
  {
    auto wall = object_manager_->NewObject<PObject>();
    wall->Add<Transform>(glm::vec3(scaled_width / 2.0f, 0.0f, 0));
    physics_engine_->RegisterBody(wall, Physics::Plane(glm::vec3(-1, 0, 0)),
                                  JPH::EAllowedDOFs::All);
  }
  {
    auto roof = object_manager_->NewObject<PObject>();
    roof->Add<Transform>(glm::vec3(0.0f, scaled_height / 2.0f, 0.0f));
    physics_engine_->RegisterBody(roof, Physics::Plane(glm::vec3(0, -1, 0)),
                                  JPH::EAllowedDOFs::All);
    physics_engine_->AddCollisionCallback<kCollisionEnter>(
        roof->id, [this](kEntityId self, kEntityId other) {
          physics_engine_->DisableBody(other);
          object_manager_->DeleteObject(other);
        });
  }

  {  // Enemy code here
    auto pos = offset;
    auto size = glm::vec2(16.0f);
    for (int i = 0; i < 5; i++) {
      pos.x = offset.x;
      for (int j = 0; j < 8; j++) {
        auto enemy = object_manager_->NewObject<Box2D>(pos, size);
        auto texture_path = std::filesystem::path(PEQUOD_ASSET_PATH) /
                            "space_invaders/enemy1.png";
        enemy->Add<Texture2D>(texture_path.string());
        physics_engine_->RegisterBody(enemy,
                                      Physics::Box(size - glm::vec2(2.0f)),
                                      JPH::EAllowedDOFs::Plane2D);
        physics_engine_->Set<kMotionType>(*enemy, JPH::EMotionType::Kinematic);
        physics_engine_->AddCollisionCallback<kCollisionEnter>(
            enemy->id, [this](kEntityId self, kEntityId other) {
              physics_engine_->DisableBody(other);
              physics_engine_->DisableBody(self);

              object_manager_->DeleteObject(other);
              object_manager_->DeleteObject(self);
            });

        pos.x -= size.x + 4;
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
}
void SpaceInvaders::OnTick(double delta_t) {
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

  if (input_manager_->IsJustPressed(GLFW_KEY_SPACE)) {
    ShootPellet();
  }
}
void SpaceInvaders::ShootPellet() {
  auto transform = player_->Get<Transform>();
  auto pos = transform->GetPosition();
  pos.y += 12;

  auto pellet_size = glm::vec2(1, 6);
  auto pellet = object_manager_->NewObject<Box2D>(pos, pellet_size);
  auto pellet_transform = pellet->Get<Transform>();
  physics_engine_->RegisterBody(pellet, Physics::Box(pellet_size),
                                JPH::EAllowedDOFs::TranslationY);
  physics_engine_->Set<kFriction>(*pellet, 0.0f);
  physics_engine_->Set<kGravity>(*pellet, 0.0f);
  physics_engine_->Set<kRestitution>(*pellet, 0.0f);
  physics_engine_->Set<kMotionType>(*pellet, JPH::EMotionType::Dynamic);
  pellet_transform->SetVelocity(glm::vec3(0, PELLET_SPEED, 0));
}
