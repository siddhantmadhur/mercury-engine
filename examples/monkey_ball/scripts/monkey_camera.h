#ifndef MONKEY_BALL_MONKEY_CAMERA_H
#define MONKEY_BALL_MONKEY_CAMERA_H

#include "scripting/behaviour.h"

// Smooth third-person chase camera for Monkey Ball with manual orbit control.
class MonkeyCamera : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("target", target).Tooltip("Node to follow (typically Player)");
    r.Field("distance", distance).Tooltip("Distance behind the ball");
    r.Field("height", height).Tooltip("Height above the ball");
    r.Field("pitch", pitch).Tooltip("Downwards look angle in degrees");
    r.Field("yaw", yaw).Tooltip("Horizontal orbit angle in degrees");
    r.Field("turn_speed", turn_speed)
        .Tooltip("Rotation speed in degrees/sec when Q/E are held");
    r.Field("smooth_speed", smooth_speed).Tooltip("Follow interpolation rate");
  }

  void OnStart() override;
  void OnTick(double dt) override;

  Mercury::NodeRef target{"Player"};
  float distance = 7.0f;
  float height = 3.5f;
  float pitch = -22.0f;
  float yaw = 0.0f;
  float turn_speed = 90.0f;
  float smooth_speed = 10.0f;

 private:
  Mercury::GameObject target_object_;
};

#endif  // MONKEY_BALL_MONKEY_CAMERA_H
