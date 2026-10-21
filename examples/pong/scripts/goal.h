#ifndef PONG_GOAL_H
#define PONG_GOAL_H

#include "scripting/behaviour.h"

enum class Side { kPlayer, kEnemy };

// A wall that scores a point for `scorer` when the ball reaches it.
class Goal : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Enum("scorer", scorer, {"player", "enemy"})
        .Tooltip("Who gets the point when the ball hits this wall");
    r.Field("game", game).Tooltip("Node with the PongGame script");
  }

  void OnCollisionEnter(Mercury::GameObject other) override;

  Side scorer = Side::kPlayer;
  Mercury::NodeRef game{"."};
};

#endif  // PONG_GOAL_H
