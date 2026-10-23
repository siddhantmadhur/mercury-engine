#ifndef MODEL_VIEWER_FLYTHROUGH_CAMERA_H
#define MODEL_VIEWER_FLYTHROUGH_CAMERA_H

#include <string>
#include <vector>

#include "scripting/behaviour.h"

// Flies the camera slowly through a scene along a smooth path, for
// benchmarking: the same views every lap, with frame times recorded per lap.
//
// The path is the children of `path` (P0, P1, ...): the camera passes
// through each one's position and turns to face the way it faces.
// Space pauses.
class FlythroughCamera : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("path", path).Tooltip("Node whose children are the waypoints, in order");
    r.Field("lap_seconds", lap_seconds).Tooltip("Time for one lap of the path");
    r.Field("loop", loop).Tooltip("Return to the first waypoint at the end");
  }
  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

  // "avg 143.2 fps · 1% low 98.1 · min 80.4" for the last full lap.
  const std::string& LastLapSummary() const { return last_lap_; }
  int Laps() const { return laps_; }
  float LapProgress() const;

  Mercury::NodeRef path{"Path"};
  float lap_seconds = 60.0f;
  bool loop = true;

 private:
  struct Waypoint {
    glm::vec3 position;
    glm::quat rotation;
  };
  void Place(float t);
  void FinishLap();

  std::vector<Waypoint> points_;
  float time_ = 0.0f;  // seconds along the path
  bool paused_ = false;
  float warmup_ = 1.0f;  // ignore the first frames (uploads, first-use stalls)
  std::vector<float> frame_times_;
  std::string last_lap_;
  int laps_ = 0;
};

#endif  // MODEL_VIEWER_FLYTHROUGH_CAMERA_H
