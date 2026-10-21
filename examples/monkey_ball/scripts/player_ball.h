#ifndef MONKEY_BALL_PLAYER_BALL_H
#define MONKEY_BALL_PLAYER_BALL_H

#include "scripting/behaviour.h"

class PlayerBall : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("roll_force", roll_force)
        .Tooltip("Continuous push force applied when rolling");
    r.Field("jump_impulse", jump_impulse)
        .Tooltip("Upward impulse applied when Space is pressed");
    r.Field("max_speed", max_speed)
        .Tooltip("Maximum horizontal speed before damping is applied");
    r.Field("camera_node", camera_node)
        .Tooltip("Camera node for orienting movement direction");
  }

  void OnStart() override;
  void OnTick(double dt) override;
  void OnCollisionEnter(Mercury::GameObject other) override;
  void OnCollisionLeave(Mercury::GameObject other) override;

  float roll_force = 24.0f;
  float jump_impulse = 7.5f;
  float max_speed = 20.0f;
  Mercury::NodeRef camera_node{"Camera"};

  bool IsGrounded() const { return grounded_; }
  void ResetPhysics();

 private:
  int contact_count_ = 0;
  bool grounded_ = false;
  float jump_cooldown_ = 0.0f;
};

#endif  // MONKEY_BALL_PLAYER_BALL_H
