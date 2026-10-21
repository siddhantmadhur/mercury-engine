#ifndef MONKEY_BALL_GOAL_TRIGGER_H
#define MONKEY_BALL_GOAL_TRIGGER_H

#include "scripting/behaviour.h"

class GoalTrigger : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("game_manager", game_manager).Tooltip("Node with GameManager script");
  }

  void OnCollisionEnter(Mercury::GameObject other) override;

  Mercury::NodeRef game_manager{"GameManager"};

 private:
  bool triggered_ = false;
};

#endif  // MONKEY_BALL_GOAL_TRIGGER_H
