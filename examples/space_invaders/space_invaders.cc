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

constexpr float ZOOM = 2.0f;
constexpr float PLAYER_SPEED = 20.0f;
#define scaled_width (GetWidth() / (ZOOM))
#define scaled_height (GetHeight() / (ZOOM))

void SpaceInvaders::OnStart() {
  {  // Camera
    player_camera_ = std::make_unique<Camera>(GetWidth() / GetHeight());
    player_camera_->configure2D(GetWidth(), GetHeight(), ZOOM);
  }
  glm::vec2 offset =
      (glm::vec2(scaled_width, scaled_height) / 2.0f) - glm::vec2(32);
  {
    player_ = object_manager_->NewObject<Box2D>(glm::vec2(0, -offset.y),
                                                glm::vec2(12.0f));
    auto texture_path = std::filesystem::path(PEQUOD_ASSET_PATH) / "wall.jpg";
    player_->Add<Texture2D>(texture_path.string());
    physics_engine_->RegisterBody(player_, Physics::Box(glm::vec2(12.0f)),
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
}
