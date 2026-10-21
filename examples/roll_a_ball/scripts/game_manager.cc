#include "game_manager.h"

#include "imgui.h"
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
  ImGui::SetNextWindowPos(ImVec2(16, 70));
  ImGui::Begin("Score", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking);
  ImGui::Text("Score: %d / %d", score_, total_);
  ImGui::TextDisabled("WASD / arrows: roll   Space: jump   Esc: quit");
  if (total_ > 0 && score_ >= total_) ImGui::Text("You win!");
  ImGui::End();
}
