//
// Created by smadhur on 4/20/2026.
//

#include "scene.h"
#include <properties/camera.h>

namespace Mercury {

GameScene::GameScene() {
  this->object_manager_ = std::make_shared<PObjectManager>();
  this->physics_engine_ = std::make_shared<PhysicsEngine>(*object_manager_);

  physics_engine_->Initialize();
  this->player_camera_ = object_manager_->NewObject();
  object_manager_->AddProperty(player_camera_, Camera());
  object_manager_->AddProperty(player_camera_, Transform(glm::vec3(0.0)));
}

bool GameScene::GetCameraProj(glm::mat4x4& proj) {
  auto cam = object_manager_->GetProperty<Camera>(player_camera_);
  auto pos = object_manager_->GetProperty<Transform>(player_camera_);
  if (!cam || !pos) {
    return false;
  }
  proj = cam->GetProjection(width_, height_) * cam->GetView(*pos);

  return true;
}
bool GameScene::GetSkyboxMatrix(glm::mat4x4& matrix) {
  glm::mat4x4 view_proj;
  glm::vec3 position;
  return GetCameraMatrices(player_camera_, width_, height_, view_proj, matrix,
                           position);
}
bool GameScene::GetCameraMatrices(kEntityId camera, float width, float height,
                                  glm::mat4x4& view_proj, glm::mat4x4& skybox,
                                  glm::vec3& position) {
  auto cam = object_manager_->GetProperty<Camera>(camera);
  auto transform = object_manager_->GetProperty<Transform>(camera);
  if (!cam || !transform) {
    return false;
  }
  const glm::mat4 view = cam->GetView(*transform);
  const glm::mat4 proj = cam->GetProjection(width, height);
  view_proj = proj * view;
  skybox = glm::inverse(proj * glm::mat4(glm::mat3(view)));
  position = transform->GetInterpolatedPosition();
  return true;
}
bool GameScene::GetCameraPosition(glm::vec3& position) {
  auto transform = object_manager_->GetProperty<Transform>(player_camera_);
  if (!transform) return false;
  position = transform->GetInterpolatedPosition();
  return true;
}

std::vector<StaticVertex> GameScene::GetStaticVertices() {
  return object_manager_->GetStaticVertices();
}
std::vector<UINT> GameScene::GetStaticIndices() {
  return object_manager_->GetStaticIndices();
}
TextureAtlas& GameScene::GetAtlas() { return object_manager_->GetAtlas(); }
float GameScene::GetWidth() const { return this->width_; }
void GameScene::SetHeight(float height) { this->height_ = height; }
void GameScene::SetInputManager(InputManager* input_manager) {
  this->input_manager_ = input_manager;
}
void GameScene::QuitScene() { should_close = true; }
bool GameScene::ShouldQuit() const { return should_close; }
void GameScene::SimulatePhysics(int steps) {
  if (steps < 5) physics_engine_->Compute(steps);
}
void GameScene::ProcessOnFrame(float alpha) {
  object_manager_->ProcessOnFrame(alpha);
}
void GameScene::OnTickBegin() { object_manager_->CaptureTickSnapshots(); }
void GameScene::SetWidth(float width) { this->width_ = width; }
float GameScene::GetHeight() const { return this->height_; }

}  // namespace Mercury