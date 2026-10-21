#include "pong_game.h"

#include "ball.h"
#include "imgui.h"

MERCURY_SCRIPT(PongGame);

void PongGame::OnStart() {
  ball_ = Get(ball).GetBehaviour<Ball>();
  if (!ball_) PDebug::error("PongGame: \"{}\" has no Ball script", ball.path);
}

void PongGame::Score(Side scorer) {
  if (scored_this_tick_) return;
  scored_this_tick_ = true;
  if (scorer == Side::kPlayer) {
    PDebug::log("Player scored");
    player_wins_ += 1;
  } else {
    PDebug::log("Enemy scored");
    enemy_wins_ += 1;
  }
  if (ball_) ball_->Serve();
}

void PongGame::OnTick(double dt) {
  // Collisions are handled before OnTick, so this clears last tick's point.
  scored_this_tick_ = false;
  if (IsKeyPressed(GLFW_KEY_ESCAPE)) Quit();
}

void PongGame::OnFrame(double dt) {
  if (show_debug && ball_) {
    auto& transform = ball_->GetTransform();
    auto pos = transform.GetPosition();
    auto vel = transform.GetVelocity();
    ImGui::SetNextWindowPos(ImVec2(10, 70), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ball");
    ImGui::Text("POSITION X: %f, Y: %f", pos.x, pos.y);
    ImGui::Text("VELOCITY X: %f, Y: %f", vel.x, vel.y);
    ImGui::End();
  }

  ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
  ImGui::Begin("Scores");
  ImGui::Text("Player: %d", player_wins_);
  ImGui::Text("Enemy: %d", enemy_wins_);
  ImGui::End();
}
