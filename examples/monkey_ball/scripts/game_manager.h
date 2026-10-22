#ifndef MONKEY_BALL_GAME_MANAGER_H
#define MONKEY_BALL_GAME_MANAGER_H

#include "scripting/behaviour.h"

enum class StageState {
  kPlaying,
  kGoal,
  kFallOut,
};

class GameManager : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("player", player).Tooltip("Player ball node");
    r.Field("fall_limit", fall_limit).Tooltip("Y height below which player falls out");
    r.Field("time_limit", time_limit).Tooltip("Stage countdown time limit in seconds");
  }

  // Values the HUD (ui/hud.uxml) binds to.
  void ReflectData(Mercury::Reflector& r) override;

  void OnStart() override;
  void OnTick(double dt) override;
  void OnFrame(double dt) override;

  void CollectBanana(int points);
  void OnGoalReached();
  void RestartStage();
  bool IsPaused() const { return paused_; }

  Mercury::NodeRef player{"Player"};
  float fall_limit = -12.0f;
  float time_limit = 60.0f;

 private:
  StageState state_ = StageState::kPlaying;
  int score_ = 0;
  int bananas_collected_ = 0;
  int total_bananas_ = 0;
  float time_remaining_ = 60.0f;
  float elapsed_time_ = 0.0f;
  int fall_count_ = 0;
  float message_timer_ = 0.0f;
  glm::vec3 start_position_{0.0f, 1.2f, 0.0f};
  bool paused_ = false;

  void SetupHud();
  void SetPaused(bool paused);
  Mercury::UI::VisualElement* hud_ = nullptr;
};

#endif  // MONKEY_BALL_GAME_MANAGER_H
