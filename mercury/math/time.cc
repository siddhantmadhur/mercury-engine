// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "time.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace Mercury {

namespace {

struct TimeState {
  double time = 0.0;        // scaled frame time
  double fixed_time = 0.0;  // scaled time of the last tick
  double realtime = 0.0;
  float delta_time = 0.0f;
  float fixed_delta_time = 1.0f / 60.0f;
  float time_scale = 1.0f;
  uint64_t frame_count = 0;
  bool in_tick = false;
};

TimeState& State() {
  static TimeState state;
  return state;
}

// Time is process-global and scripts can read it from worker threads, so the
// whole state is guarded. The critical sections are tiny.
std::mutex& StateMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

double Time::GetTime() {
  std::lock_guard lock(StateMutex());
  const auto& s = State();
  return s.in_tick ? s.fixed_time : s.time;
}

float Time::GetDeltaTime() {
  std::lock_guard lock(StateMutex());
  const auto& s = State();
  return s.in_tick ? s.fixed_delta_time * s.time_scale : s.delta_time;
}

float Time::GetFixedDeltaTime() {
  std::lock_guard lock(StateMutex());
  return State().fixed_delta_time;
}

double Time::GetRealtimeSinceStartup() {
  std::lock_guard lock(StateMutex());
  return State().realtime;
}

uint64_t Time::GetFrameCount() {
  std::lock_guard lock(StateMutex());
  return State().frame_count;
}

bool Time::InTick() {
  std::lock_guard lock(StateMutex());
  return State().in_tick;
}

float Time::GetTimeScale() {
  std::lock_guard lock(StateMutex());
  return State().time_scale;
}

void Time::SetTimeScale(float scale) {
  // NaN compares false with everything, so it used to slip past the old clamp
  // and poison every later delta and fixed step.
  if (!std::isfinite(scale)) return;
  std::lock_guard lock(StateMutex());
  State().time_scale = std::max(0.0f, scale);
}

void Time::Reset(float fixed_delta_time) {
  std::lock_guard lock(StateMutex());
  State() = TimeState{};
  State().fixed_delta_time = fixed_delta_time;
}

void Time::BeginFrame(double real_delta_seconds) {
  std::lock_guard lock(StateMutex());
  auto& s = State();
  // Long stalls (a breakpoint, loading) should not fast-forward the game.
  if (real_delta_seconds > 0.25) real_delta_seconds = 0.25;
  if (real_delta_seconds < 0.0) real_delta_seconds = 0.0;
  s.realtime += real_delta_seconds;
  s.delta_time = static_cast<float>(real_delta_seconds) * s.time_scale;
  s.time += s.delta_time;
  s.frame_count++;
}

void Time::BeginTick() {
  std::lock_guard lock(StateMutex());
  auto& s = State();
  s.in_tick = true;
  s.fixed_time += s.fixed_delta_time * s.time_scale;
}

void Time::EndTick() {
  std::lock_guard lock(StateMutex());
  State().in_tick = false;
}

}  // namespace Mercury
