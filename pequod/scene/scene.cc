//
// Created by smadhur on 4/20/2026.
//

#include "scene.h"

namespace Pequod {

GameScene::GameScene() {
  this->object_manager_ = std::make_unique<PObjectManager>();
  this->physics_engine_ = std::make_unique<PhysicsEngine>();

  physics_engine_->Initialize();
  this->player_camera_ = object_manager_->NewObject<Camera2D>(width_, height_);
}

bool GameScene::GetCameraProj(glm::mat4x4& proj) {
  if (this->player_camera_ == nullptr) {
    return false;
  }

  proj = player_camera_->GetProjection() * player_camera_->GetView();

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