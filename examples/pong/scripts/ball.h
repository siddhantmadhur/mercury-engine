#ifndef PONG_BALL_H
#define PONG_BALL_H

#include "scripting/behaviour.h"

/**
 * @brief Serves the ball and keeps it moving at a constant speed
 *
 * Moving paddles add energy on contact, and a near-vertical ball would never
 * reach either side, so OnTick corrects both.
 */
class Ball : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("speed", speed).Tooltip("World units per second");
    r.Field("min_horizontal", min_horizontal)
        .Tooltip("Smallest share of the direction that is horizontal");
    r.Field("serve_angle", serve_angle)
        .Tooltip("Serves go within this many degrees of horizontal");
  }

  void OnStart() override;
  void OnTick(double dt) override;

  // Back to the center, heading towards a random side. Only the first call
  // in a tick counts.
  void Serve();

  float speed = 60.0f;
  float min_horizontal = 0.45f;
  float serve_angle = 35.0f;

 private:
  bool served_this_tick_ = false;
};

#endif  // PONG_BALL_H
