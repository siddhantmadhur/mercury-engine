/*
 * time.h
 *
 * Game time, the C++ version of Unity's Time class. MapScene advances it, so
 * scripts can read it from anywhere:
 *
 *   position += velocity * Mercury::Time::GetDeltaTime();
 *
 * Inside OnTick, GetDeltaTime() is the fixed tick length, as Unity's
 * Time.deltaTime is inside FixedUpdate.
 */
#ifndef MERCURY_MATH_TIME_H
#define MERCURY_MATH_TIME_H

#include <cstdint>

namespace Mercury {

class Time {
 public:
  // Seconds since the scene started; during a tick, the time of that tick.
  static double GetTime();
  // Seconds since the last frame, or the fixed tick length inside a tick.
  static float GetDeltaTime();
  // Length of one fixed tick in seconds (1 / tick rate).
  static float GetFixedDeltaTime();
  // Unscaled time since the scene started.
  static double GetRealtimeSinceStartup();
  // Frames rendered since the scene started.
  static uint64_t GetFrameCount();
  // Multiplies how fast game time passes (0 pauses ticks), like
  // Time.timeScale.
  static float GetTimeScale();
  static void SetTimeScale(float scale);

  // Called by MapScene.
  static void Reset(float fixed_delta_time);
  static void BeginFrame(double real_delta_seconds);
  static void BeginTick();
  static void EndTick();
};

}  // namespace Mercury

#endif  // MERCURY_MATH_TIME_H
