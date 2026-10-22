#include "game_manager.h"

#include "pickup.h"

MERCURY_SCRIPT(GameManager);

void GameManager::OnStart() {
  // Every pickup in the map counts towards the total.
  auto& registry = GetScene().GetRegistry();
  for (auto entity : registry.view<Mercury::NodeInfo>()) {
    if (auto pickup = GetScene().GetGameObject(entity).GetBehaviour<Pickup>()) {
      total_ += pickup->points;
    }
  }
  if (auto object = Get(player)) {
    start_position_ = object.GetTransform()->GetPosition();
  }
  // The score label in score.uxml binds to score_text (see ReflectData).
  if (auto* document = GetDocument(); document && document->rootVisualElement()) {
    document->rootVisualElement()->SetDataSource(AsDataSource());
  }
}

void GameManager::Collect(int points) {
  score_ += points;
  PDebug::log("Picked up {} of {}", score_, total_);
}

void GameManager::OnTick(double dt) {
  if (IsKeyPressed(GLFW_KEY_ESCAPE)) Quit();
  auto object = Get(player);
  if (!object) return;
  auto* transform = object.GetTransform();
  if (transform->GetPosition().y < fall_limit) {
    transform->SetPosition(start_position_);
    transform->SetVelocity(glm::vec3(0.0f));
  }
}

void GameManager::OnFrame(double dt) {
  auto* document = GetDocument();
  if (!document || !document->rootVisualElement()) return;
  document->rootVisualElement()->Q("win")->EnableInClassList(
      "win--hidden", total_ == 0 || score_ < total_);
}
