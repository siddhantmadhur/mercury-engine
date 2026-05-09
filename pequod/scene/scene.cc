//
// Created by smadhur on 4/20/2026.
//

#include "scene.h"
#include <properties/camera.h>

namespace Pequod {

GameScene::GameScene() {
  this->object_manager_ = std::make_unique<PObjectManager>();
  this->physics_engine_ = std::make_unique<PhysicsEngine>(*object_manager_);

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
  proj = cam->GetProjection(width_, height_) * cam->GetView(pos->GetPosition());

  return true;
}
std::vector<Primitive> GameScene::GetPrimitives() {
  return object_manager_->GetPrimitives();
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
void GameScene::SimulatePhysics() { physics_engine_->Compute(1); }
void GameScene::SetWidth(float width) { this->width_ = width; }
float GameScene::GetHeight() const { return this->height_; }

}  // namespace Pequod