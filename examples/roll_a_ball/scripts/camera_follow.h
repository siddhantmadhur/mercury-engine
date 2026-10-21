#ifndef ROLL_A_BALL_CAMERA_FOLLOW_H
#define ROLL_A_BALL_CAMERA_FOLLOW_H

#include "scripting/behaviour.h"

// Keeps the camera at the offset it starts with from the target.
class CameraFollow : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("target", target).Tooltip("Node to follow");
  }

  void OnStart() override;
  void OnTick(double dt) override;

  Mercury::NodeRef target;

 private:
  Mercury::GameObject target_;
  glm::vec3 offset_ = glm::vec3(0.0f);
};

#endif  // ROLL_A_BALL_CAMERA_FOLLOW_H
