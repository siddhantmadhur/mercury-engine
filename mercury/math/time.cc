#include "time.h"

namespace Mercury {

namespace {

struct TimeState {
  double time = 0.0;           // scaled frame time
  double fixed_time = 0.0;     // scaled time of the last tick
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

}  // namespace

double Time::GetTime() {
  const auto& s = State();
  return s.in_tick ? s.fixed_time : s.time;
}

float Time::GetDeltaTime() {
  const auto& s = State();
  return s.in_tick ? s.fixed_delta_time * s.time_scale : s.delta_time;
}

float Time::GetFixedDeltaTime() { return State().fixed_delta_time; }

double Time::GetRealtimeSinceStartup() { return State().realtime; }

uint64_t Time::GetFrameCount() { return State().frame_count; }

float Time::GetTimeScale() { return State().time_scale; }

void Time::SetTimeScale(float scale) { State().time_scale = scale < 0 ? 0 : scale; }

void Time::Reset(float fixed_delta_time) {
  State() = TimeState{};
  State().fixed_delta_time = fixed_delta_time;
}

void Time::BeginFrame(double real_delta_seconds) {
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
  auto& s = State();
  s.in_tick = true;
  s.fixed_time += s.fixed_delta_time * s.time_scale;
}

void Time::EndTick() { State().in_tick = false; }

}  // namespace Mercury
