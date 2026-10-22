#include "game_manager.h"

#include <algorithm>
#include <format>

#include "banana_pickup.h"
#include "player_ball.h"
#include "ui/controls.h"

MERCURY_SCRIPT(GameManager);

using namespace Mercury::UI;

void GameManager::ReflectData(Mercury::Reflector& r) {
  r.Field("score", score_);
  r.Field("falls", fall_count_);
  r.Field("time_remaining", time_remaining_);
  r.Property("time_text", [this] {
    return Mercury::mtext::Variant(std::format("{:.2f} s", time_remaining_));
  });
  r.Property("bananas_text", [this] {
    return Mercury::mtext::Variant(
        std::format("{} / {}", bananas_collected_, total_bananas_));
  });
}

void GameManager::SetupHud() {
  auto* document = GetDocument();
  hud_ = document ? document->rootVisualElement() : nullptr;
  if (!hud_) return;
  // Labels in hud.uxml bind to the fields listed in ReflectData.
  hud_->SetDataSource(AsDataSource());

  hud_->Q<Button>("resume")->clicked += [this] { SetPaused(false); };
  hud_->Q<Button>("restart")->clicked += [this] {
    SetPaused(false);
    RestartStage();
  };
  hud_->Q<Button>("play-again")->clicked += [this] { RestartStage(); };
  hud_->Q<Button>("quit")->clicked += [this] { Quit(); };
  hud_->Q<Button>("goal-quit")->clicked += [this] { Quit(); };
  hud_->Q<Toggle>("show-hints")->RegisterValueChangedCallback<bool>(
      [this](ChangeEvent<bool>& e) {
        hud_->Q<Label>({}, "hint")->EnableInClassList("hint--hidden", !e.newValue);
      });
}

void GameManager::SetPaused(bool paused) {
  paused_ = paused;
  if (!hud_) return;
  hud_->Q("pause")->EnableInClassList("hidden", !paused);
  if (paused) {
    hud_->Q<Button>("resume")->Focus();
  } else if (auto* focused = hud_->panel()->focusedElement()) {
    // So Enter/arrow keys go back to the game.
    focused->Blur();
  }
}

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
  SetupHud();
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
  if (IsKeyJustPressed(GLFW_KEY_ESCAPE)) SetPaused(!paused_);
  if (paused_) return;
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
  if (!hud_) return;
  // Values update through bindings; only classes are toggled here, and
  // hud.uss animates the changes.
  hud_->Q<Label>("time")->EnableInClassList("stat__value--low",
                                            time_remaining_ < 10.0f);
  hud_->Q("fall-out")->EnableInClassList("banner--hidden",
                                         state_ != StageState::kFallOut);
  auto* goal = hud_->Q("goal");
  const bool cleared = state_ == StageState::kGoal;
  if (cleared && goal->ClassListContains("banner--hidden")) {
    hud_->Q<Label>("goal-summary")
        ->SetText(std::format(
            "Time <b>{:.2f}s</b>   Bananas <color=#FFE433><b>{} / {}</b></color>\n"
            "Score <b>{}</b>   Falls <b>{}</b>",
            elapsed_time_, bananas_collected_, total_bananas_, score_, fall_count_));
  }
  goal->EnableInClassList("banner--hidden", !cleared);
}
