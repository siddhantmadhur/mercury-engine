#ifndef MODEL_VIEWER_ORBIT_CAMERA_H
#define MODEL_VIEWER_ORBIT_CAMERA_H

#include "scripting/behaviour.h"

// Turns the camera around a point with the mouse, like a model viewer:
// left-drag orbits, right/middle-drag pans, the wheel zooms, R resets.
// With `distance` 0 the camera frames `target`'s mesh by itself.
class OrbitCamera : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("target", target).Tooltip("Node whose mesh to frame (when distance is 0)");
    r.Field("focus", focus).Tooltip("Point to orbit around");
    r.Field("distance", distance).Tooltip("Distance from the focus; 0 frames the target");
    r.Field("yaw", yaw).Tooltip("Starting angle around the vertical, degrees");
    r.Field("pitch", pitch).Tooltip("Starting angle above the horizon, degrees");
    r.Field("auto_rotate", auto_rotate).Tooltip("Degrees per second while idle");
  }
  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

  Mercury::NodeRef target;
  glm::vec3 focus = glm::vec3(0.0f);
  float distance = 0.0f;
  float yaw = 0.0f;
  float pitch = 15.0f;
  float auto_rotate = 6.0f;

 private:
  void Apply();

  glm::vec3 start_focus_ = glm::vec3(0.0f);
  float start_distance_ = 1.0f;
  float start_yaw_ = 0.0f;
  float start_pitch_ = 0.0f;
  float idle_ = 0.0f;  // seconds since the last mouse input
  float scroll_ = 0.0f;  // wheel turned since the last tick
};

#endif  // MODEL_VIEWER_ORBIT_CAMERA_H
