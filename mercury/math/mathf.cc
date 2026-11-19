// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mathf.h"

#include <mutex>
#include <random>

namespace Mercury {

float Mathf::SmoothDamp(float current, float target, float& velocity,
                        float smooth_time, float delta_time, float max_speed) {
  // A zero or backwards step has no meaning, and dividing by it below would
  // turn the velocity into NaN.
  if (delta_time <= 0.0f) {
    velocity = 0.0f;
    return current;
  }
  // Game Programming Gems 4, chapter 1.10, as Unity implements it.
  smooth_time = glm::max(0.0001f, smooth_time);
  const float omega = 2.0f / smooth_time;
  const float x = omega * delta_time;
  const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
  float change = current - target;
  const float original_to = target;

  const float max_change = max_speed * smooth_time;
  change = Clamp(change, -max_change, max_change);
  target = current - change;

  const float temp = (velocity + omega * change) * delta_time;
  velocity = (velocity - omega * temp) * exp;
  float output = target + (change + temp) * exp;

  // Do not overshoot.
  if ((original_to - current > 0.0f) == (output > original_to)) {
    output = original_to;
    velocity = (output - original_to) / delta_time;
  }
  return output;
}

namespace {
// One engine keeps InitState() reproducible, but Range() can be called from
// ParallelFor workers (through scripts), so every use is serialized.
std::mt19937& Engine() {
  static std::mt19937 engine{std::random_device{}()};
  return engine;
}

std::mutex& EngineMutex() {
  static std::mutex mutex;
  return mutex;
}
}  // namespace

float Random::Range(float min, float max) {
  if (min > max) std::swap(min, max);
  std::uniform_real_distribution<float> distribution(
      min, std::nextafter(max, max + 1.0f));
  std::lock_guard lock(EngineMutex());
  return distribution(Engine());
}

int Random::Range(int min, int max) {
  if (min >= max) return min;
  std::uniform_int_distribution<int> distribution(min, max - 1);
  std::lock_guard lock(EngineMutex());
  return distribution(Engine());
}

float Random::Value() { return Range(0.0f, 1.0f); }

glm::vec3 Random::InsideUnitSphere() {
  while (true) {
    glm::vec3 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
    if (glm::dot(p, p) <= 1.0f) return p;
  }
}

glm::vec2 Random::InsideUnitCircle() {
  while (true) {
    glm::vec2 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
    if (glm::dot(p, p) <= 1.0f) return p;
  }
}

void Random::InitState(uint32_t seed) {
  std::lock_guard lock(EngineMutex());
  Engine().seed(seed);
}

}  // namespace Mercury
