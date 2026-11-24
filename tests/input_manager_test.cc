// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include "GLFW/glfw3.h"
#include "input/input_manager.h"
#include "input/input_script.h"
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

TEST(InputScriptTest, DumpAcceptsNoNodesAWildcardAndSemicolonPaths) {
  InputScript script;
  std::string error;
  ASSERT_TRUE(script.LoadFromString(R"(1 dump out.json
2 dump every.json *
3 dump some.json Root/Player; Ball
4 dump spaces.json Main Camera ;  Player/Camera
5 dump trailing.json Extra;
6 dump star.json Root; *; Other
)",
                                    &error))
      << error;

  struct Call {
    std::string file;
    std::vector<std::string> nodes;
  };
  std::vector<Call> calls;
  InputScript::Hooks hooks;
  hooks.dump = [&](const std::string& file,
                   const std::vector<std::string>& nodes) {
    calls.push_back({file, nodes});
  };
  InputManager input;
  script.Apply(6, input, nullptr, hooks);

  ASSERT_EQ(calls.size(), 6u);
  EXPECT_EQ(calls[0].file, "out.json");
  EXPECT_TRUE(calls[0].nodes.empty());
  EXPECT_EQ(calls[1].file, "every.json");
  EXPECT_TRUE(calls[1].nodes.empty());  // `*` was normalized to all nodes
  EXPECT_EQ(calls[2].file, "some.json");
  EXPECT_EQ(calls[2].nodes, (std::vector<std::string>{"Root/Player", "Ball"}));
  EXPECT_EQ(calls[3].file, "spaces.json");
  EXPECT_EQ(calls[3].nodes,
            (std::vector<std::string>{"Main Camera", "Player/Camera"}));
  EXPECT_EQ(calls[4].file, "trailing.json");
  EXPECT_EQ(calls[4].nodes, (std::vector<std::string>{"Extra"}));
  EXPECT_EQ(calls[5].file, "star.json");
  EXPECT_TRUE(calls[5].nodes.empty());  // a `*` entry means every node
}

TEST(InputScriptTest, DumpNeedsAFileArgument) {
  InputScript script;
  std::string error;
  EXPECT_FALSE(script.LoadFromString("1 dump\n", &error));
  EXPECT_NE(error.find("dump needs a file"), std::string::npos) << error;
}

}  // namespace
}  // namespace Mercury
