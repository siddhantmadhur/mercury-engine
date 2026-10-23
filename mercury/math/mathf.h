/*
 * mathf.h
 *
 * Unity's Mathf and Random helpers that glm does not already cover.
 */
#ifndef MERCURY_MATH_MATHF_H
#define MERCURY_MATH_MATHF_H

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Mercury {

namespace Mathf {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;
constexpr float kEpsilon = 1.17549435e-38f;
constexpr float kInfinity = __builtin_huge_valf();

inline float Clamp(float value, float min, float max) {
  return value < min ? min : (value > max ? max : value);
}
inline int Clamp(int value, int min, int max) {
  return value < min ? min : (value > max ? max : value);
}
inline float Clamp01(float value) { return Clamp(value, 0.0f, 1.0f); }

// t is clamped to [0, 1], as in Unity.
inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
inline float LerpUnclamped(float a, float b, float t) { return a + (b - a) * t; }
inline float InverseLerp(float a, float b, float value) {
  return a != b ? Clamp01((value - a) / (b - a)) : 0.0f;
}

inline float Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; }

inline bool Approximately(float a, float b) {
  const float largest = glm::max(glm::abs(a), glm::abs(b));
  return glm::abs(b - a) < glm::max(1e-6f * largest, 8.0f * 1.17549435e-38f);
}

inline float MoveTowards(float current, float target, float max_delta) {
  if (glm::abs(target - current) <= max_delta) return target;
  return current + Sign(target - current) * max_delta;
}

// Wraps t into [0, length).
inline float Repeat(float t, float length) {
  return Clamp(t - glm::floor(t / length) * length, 0.0f, length);
}

inline float PingPong(float t, float length) {
  t = Repeat(t, length * 2.0f);
  return length - glm::abs(t - length);
}

// Shortest difference between two angles in degrees.
inline float DeltaAngle(float current, float target) {
  float delta = Repeat(target - current, 360.0f);
  if (delta > 180.0f) delta -= 360.0f;
  return delta;
}

inline float LerpAngle(float a, float b, float t) {
  return a + DeltaAngle(a, b) * Clamp01(t);
}

inline float SmoothStep(float from, float to, float t) {
  t = Clamp01(t);
  t = -2.0f * t * t * t + 3.0f * t * t;
  return to * t + from * (1.0f - t);
}

/**
 * @brief Gradually changes a value towards a goal, like a spring-damper
 * (Unity's Mathf.SmoothDamp)
 */
float SmoothDamp(float current, float target, float& velocity, float smooth_time,
                 float delta_time, float max_speed = kInfinity);

}  // namespace Mathf

namespace Random {

// Uniform float in [min, max] (inclusive), as Unity's Random.Range(float).
float Range(float min, float max);
// Uniform int in [min, max) (max exclusive), as Unity's Random.Range(int).
int Range(int min, int max);
// Uniform float in [0, 1].
float Value();
glm::vec3 InsideUnitSphere();
glm::vec2 InsideUnitCircle();
void InitState(uint32_t seed);

}  // namespace Random

}  // namespace Mercury

#endif  // MERCURY_MATH_MATHF_H
