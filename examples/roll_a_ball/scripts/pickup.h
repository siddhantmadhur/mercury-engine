#ifndef ROLL_A_BALL_PICKUP_H
#define ROLL_A_BALL_PICKUP_H

#include "scripting/behaviour.h"

// Collected (and removed) when the player rolls into it.
class Pickup : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("game", game).Tooltip("Node with the GameManager script");
    r.Field("points", points);
  }

  void OnTriggerEnter(Mercury::GameObject other) override;

  Mercury::NodeRef game{"."};
  int points = 1;

 private:
  bool collected_ = false;
};

#endif  // ROLL_A_BALL_PICKUP_H
