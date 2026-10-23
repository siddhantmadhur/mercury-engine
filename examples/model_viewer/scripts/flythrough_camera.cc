#include "flythrough_camera.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include "debugger/debugger.h"

MERCURY_SCRIPT(FlythroughCamera);

namespace {

glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                     const glm::vec3& p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
}

// The number after the name's letters (P12 -> 12), so P10 follows P9.
int WaypointNumber(const std::string& name) {
  size_t i = name.size();
  while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) i--;
  return i < name.size() ? std::stoi(name.substr(i)) : 0;
}

}  // namespace

void FlythroughCamera::OnStart() {
  points_.clear();
  if (auto root = Get(path)) {
    auto children = root.GetChildren();
    std::stable_sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
      return WaypointNumber(a.GetName()) < WaypointNumber(b.GetName());
    });
    for (const auto& child : children) {
      const auto* transform = child.GetTransform();
      points_.push_back({transform->GetPosition(), transform->GetRotationQuat()});
    }
  }
  if (points_.empty()) {
    PDebug::warn("FlythroughCamera: {} has no waypoints", path.path);
    return;
  }
  Place(0.0f);
}

float FlythroughCamera::LapProgress() const {
  return lap_seconds > 0.0f ? std::fmod(time_, lap_seconds) / lap_seconds : 0.0f;
}

void FlythroughCamera::OnFrame(double dt) {
  // Real frame times, not ticks: this is what the benchmark measures.
  if (paused_ || points_.size() < 2) return;
  if (warmup_ > 0.0f) {
    warmup_ -= static_cast<float>(dt);
    return;
  }
  frame_times_.push_back(static_cast<float>(dt));
}

void FlythroughCamera::OnTick(double dt) {
  if (IsKeyJustPressed(GLFW_KEY_SPACE)) {
    paused_ = !paused_;
    // A paused lap measures nothing: start it again.
    frame_times_.clear();
  }
  if (paused_ || points_.size() < 2 || warmup_ > 0.0f) return;
  const float before = time_;
  time_ += static_cast<float>(dt);
  if (std::floor(time_ / lap_seconds) > std::floor(before / lap_seconds)) {
    FinishLap();
    if (!loop) {
      time_ = lap_seconds;
      paused_ = true;
    }
  }
  Place(std::fmod(time_, lap_seconds) / lap_seconds);
}

void FlythroughCamera::Place(float t) {
  const int count = static_cast<int>(points_.size());
  const int segments = loop ? count : count - 1;
  const float f = std::clamp(t, 0.0f, 1.0f) * segments;
  const int i = std::min(static_cast<int>(f), segments - 1);
  const float local = f - i;
  auto at = [&](int index) -> const Waypoint& {
    if (loop) return points_[((index % count) + count) % count];
    return points_[std::clamp(index, 0, count - 1)];
  };
  const glm::vec3 position =
      CatmullRom(at(i - 1).position, at(i).position, at(i + 1).position, at(i + 2).position, local);
  // Ease the turn so the camera settles into each waypoint's view.
  const float ease = local * local * (3.0f - 2.0f * local);
  const glm::quat rotation = glm::slerp(at(i).rotation, at(i + 1).rotation, ease);
  GetTransform().SetPosition(position);
  GetTransform().SetRotate(Mercury::Transform::QuatToEuler(rotation));
}

void FlythroughCamera::FinishLap() {
  laps_++;
  if (frame_times_.empty()) return;
  std::vector<float> times = frame_times_;
  frame_times_.clear();
  double total = 0.0;
  for (float t : times) total += t;
  std::sort(times.begin(), times.end());
  // 1% low: the frame rate of the slowest 1% of frames.
  const size_t worst = std::max<size_t>(1, times.size() / 100);
  double worst_total = 0.0;
  for (size_t k = times.size() - worst; k < times.size(); k++) worst_total += times[k];
  const double avg_fps = times.size() / total;
  const double low_fps = worst / worst_total;
  const double min_fps = 1.0 / times.back();
  last_lap_ = std::format("avg {:.1f} fps · 1% low {:.1f} · min {:.1f} ({:.2f} ms avg)",
                          avg_fps, low_fps, min_fps, 1000.0 * total / times.size());
  PDebug::log("Lap {}: {} frames, {}", laps_, times.size(), last_lap_);
}
