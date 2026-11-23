// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include "GLFW/glfw3.h"
#include "input/input_manager.h"
#include "math/time.h"

namespace Mercury {
namespace {

class InputManagerTest : public ::testing::Test {
 protected:
  void SetUp() override { Time::Reset(1.0f / 60.0f); }
  void TearDown() override {
    if (Time::InTick()) Time::EndTick();
  }
};

TEST_F(InputManagerTest, PressIsSeenOncePerFrameAndOncePerTick) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_PRESS);

  // Frames run twice as fast as ticks: this frame reads the press, the next
  // frame does not, and the tick that follows still sees it exactly once.
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));
  input.BeginFrameFreshPresses();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));
  input.ResetFreshPresses();
  Time::EndTick();
  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));
  Time::EndTick();
}

TEST_F(InputManagerTest, TwoTicksInOneFrameOnlyTheFirstSeesThePress) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_PRESS);

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));
  input.ResetFreshPresses();
  Time::EndTick();

  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));
  Time::EndTick();
}

TEST_F(InputManagerTest, TapWithinOneFrameIsSeenInBothPhases) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_PRESS);
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_RELEASE);

  EXPECT_FALSE(input.IsPressed(GLFW_KEY_A));
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));
  input.ResetFreshPresses();
  Time::EndTick();

  input.BeginFrameFreshPresses();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));
  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));
  Time::EndTick();
}

TEST_F(InputManagerTest, MousePressIsSeenOncePerFrameAndOncePerTick) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectMouseButtonEvent(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);

  EXPECT_TRUE(input.IsJustPressed(MouseButton::kLeft));
  input.BeginFrameFreshPresses();
  EXPECT_FALSE(input.IsJustPressed(MouseButton::kLeft));

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(MouseButton::kLeft));
  input.ResetFreshPresses();
  Time::EndTick();
  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(MouseButton::kLeft));
  Time::EndTick();
}

TEST_F(InputManagerTest, MouseTapAndRepeatDoNotClearThePendingPress) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectMouseButtonEvent(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS);
  input.InjectMouseButtonEvent(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE);
  EXPECT_FALSE(input.IsPressed(MouseButton::kRight));

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(MouseButton::kRight));
  input.ResetFreshPresses();
  Time::EndTick();

  input.BeginFrameFreshPresses();
  input.InjectMouseButtonEvent(GLFW_MOUSE_BUTTON_RIGHT, GLFW_REPEAT);
  EXPECT_TRUE(input.IsPressed(MouseButton::kRight));
  EXPECT_FALSE(input.IsJustPressed(MouseButton::kRight));
  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(MouseButton::kRight));
  Time::EndTick();
}

TEST_F(InputManagerTest, HeldKeyDoesNotRetriggerOnRepeat) {
  InputManager input;
  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_PRESS);
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_REPEAT);
  EXPECT_TRUE(input.IsPressed(GLFW_KEY_A));
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));

  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_REPEAT);
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));

  Time::BeginTick();
  EXPECT_TRUE(input.IsJustPressed(GLFW_KEY_A));
  input.ResetFreshPresses();
  Time::EndTick();

  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_A, GLFW_REPEAT);
  Time::BeginTick();
  EXPECT_FALSE(input.IsJustPressed(GLFW_KEY_A));
  Time::EndTick();

  input.InjectKeyEvent(GLFW_KEY_A, GLFW_RELEASE);
  EXPECT_FALSE(input.IsPressed(GLFW_KEY_A));
}

}  // namespace
}  // namespace Mercury
