#include "game_manager.h"

#include <algorithm>

#include "banana_pickup.h"
#include "imgui.h"
#include "player_ball.h"

MERCURY_SCRIPT(GameManager);

void GameManager::OnStart() {
  score_ = 0;
  bananas_collected_ = 0;
  total_bananas_ = 0;
  fall_count_ = 0;
  elapsed_time_ = 0.0f;
  time_remaining_ = time_limit;
  state_ = StageState::kPlaying;

  auto& registry = GetScene().GetRegistry();
  for (auto entity : registry.view<Mercury::NodeInfo>()) {
    if (GetScene().GetGameObject(entity).GetBehaviour<BananaPickup>()) {
      total_bananas_++;
    }
  }

  if (auto p = Get(player)) {
    start_position_ = p.GetTransform()->GetPosition();
  }
}

void GameManager::CollectBanana(int points) {
  bananas_collected_++;
  score_ += points;
  time_remaining_ = std::min(time_limit + 30.0f, time_remaining_ + 3.0f);
}

void GameManager::OnGoalReached() {
  if (state_ == StageState::kPlaying) {
    state_ = StageState::kGoal;
    score_ += static_cast<int>(time_remaining_ * 50.0f);
  }
}

void GameManager::RestartStage() {
  state_ = StageState::kPlaying;
  time_remaining_ = time_limit;
  elapsed_time_ = 0.0f;
  score_ = 0;
  bananas_collected_ = 0;

  if (auto p = Get(player)) {
    p.GetTransform()->SetPosition(start_position_);
    if (auto pb = p.GetBehaviour<PlayerBall>()) {
      pb->ResetPhysics();
    }
  }
}

void GameManager::OnTick(double dt) {
  if (IsKeyPressed(GLFW_KEY_ESCAPE)) Quit();
  if (IsKeyJustPressed(GLFW_KEY_R)) RestartStage();

  if (state_ == StageState::kPlaying) {
    elapsed_time_ += static_cast<float>(dt);
    time_remaining_ = std::max(0.0f, time_remaining_ - static_cast<float>(dt));

    if (auto p = Get(player)) {
      if (p.GetTransform()->GetPosition().y < fall_limit) {
        fall_count_++;
        message_timer_ = 1.5f;
        state_ = StageState::kFallOut;
        p.GetTransform()->SetPosition(start_position_);
        if (auto pb = p.GetBehaviour<PlayerBall>()) {
          pb->ResetPhysics();
        }
      }
    }
  } else if (state_ == StageState::kFallOut) {
    message_timer_ -= static_cast<float>(dt);
    if (message_timer_ <= 0.0f) {
      state_ = StageState::kPlaying;
    }
  }
}

void GameManager::OnFrame(double dt) {
  // Top HUD window
  ImGui::SetNextWindowPos(ImVec2(16, 70), ImGuiCond_Always);
  ImGui::Begin("Super Monkey Ball HUD", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking);

  ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "SUPER MONKEY BALL");
  ImGui::Separator();

  // Time display
  if (time_remaining_ < 10.0f) {
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "TIME:  %.2f s",
                       time_remaining_);
  } else {
    ImGui::Text("TIME:  %.2f s", time_remaining_);
  }

  // Bananas & Score
  ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "BANANAS: %d / %d",
                     bananas_collected_, total_bananas_);
  ImGui::Text("SCORE:   %d", score_);
  ImGui::Text("FALLS:   %d", fall_count_);

  ImGui::Separator();
  ImGui::TextDisabled("WASD / Arrows: Roll");
  ImGui::TextDisabled("Space: Jump over obstacles");
  ImGui::TextDisabled("Q / E: Rotate Camera View");
  ImGui::TextDisabled("R: Restart Stage | Esc: Quit");

  ImGui::End();

  // Fall Out banner
  if (state_ == StageState::kFallOut) {
    ImGui::SetNextWindowPos(ImVec2(540, 260), ImGuiCond_Always);
    ImGui::Begin("FallOutBanner", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.2f, 1.0f),
                       "=== FALL OUT! ===");
    ImGui::Text("Respawning at start...");
    ImGui::End();
  }

  // Stage Cleared banner
  if (state_ == StageState::kGoal) {
    ImGui::SetNextWindowPos(ImVec2(500, 220), ImGuiCond_Always);
    ImGui::Begin("GoalBanner", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                       "=== GOAL! STAGE CLEARED! ===");
    ImGui::Separator();
    ImGui::Text("Time Cleared:     %.2f seconds", elapsed_time_);
    ImGui::Text("Bananas Picked:   %d of %d", bananas_collected_, total_bananas_);
    ImGui::Text("Final Score:      %d", score_);
    ImGui::Text("Falls:            %d", fall_count_);
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f),
                       "Press 'R' to Play Again or 'Esc' to Quit");
    ImGui::End();
  }
}
