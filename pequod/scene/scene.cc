//
// Created by smadhur on 4/20/2026.
//

#include "scene.h"

namespace Pequod {

GameScene::GameScene() {
  this->object_manager_ = std::make_unique<PObjectManager>();
}
bool GameScene::GetCameraProj(glm::mat4x4& proj) {
  if (this->player_camera_ == nullptr) {
    return false;
  }

  proj = player_camera_->getProjection() * player_camera_->getView();

  return true;
}
std::vector<Primitive> GameScene::GetPrimitives() {
  return object_manager_->GetPrimitives();
}

}  // namespace Pequod