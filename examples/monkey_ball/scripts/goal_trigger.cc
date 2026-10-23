#include "goal_trigger.h"

#include "game_manager.h"
#include "player_ball.h"

MERCURY_SCRIPT(GoalTrigger);

void GoalTrigger::OnTriggerEnter(Mercury::GameObject other) {
  if (triggered_) return;
  if (!other.GetBehaviour<PlayerBall>()) return;

  triggered_ = true;
  if (auto mgr_obj = Get(game_manager)) {
    if (auto mgr = mgr_obj.GetBehaviour<GameManager>()) {
      mgr->OnGoalReached();
      return;
    }
  }

  // Fallback: search scene for GameManager
  auto& registry = GetScene().GetRegistry();
  for (auto entity : registry.view<Mercury::NodeInfo>()) {
    if (auto mgr = GetScene().GetGameObject(entity).GetBehaviour<GameManager>()) {
      mgr->OnGoalReached();
      break;
    }
  }
}
