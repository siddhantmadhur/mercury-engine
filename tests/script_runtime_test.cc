// Tests of the embedded AngelScript engine itself: evaluation, exceptions and
// the infinite-loop guard.
#include <gtest/gtest.h>

#include <string>

#include "scripting/as/script_runtime.h"

using namespace Mercury;

namespace {

class ScriptRuntimeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(ScriptRuntime::Instance().Initialize()); }
  void TearDown() override { ScriptRuntime::Instance().Shutdown(); }
};

const ::testing::Environment* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new ScriptRuntimeEnvironment);

}  // namespace

TEST(ScriptRuntime, EvaluatesAString) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString("string main() { return \"hello\"; }", "test",
                                                  &result));
  EXPECT_EQ(result, "hello");
}

TEST(ScriptRuntime, ConstructsAValueType) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString(
      "string main() { Vector3 v(1.0f, 2.0f, 3.0f); return \"\" + v.y; }", "vec", &result));
  EXPECT_EQ(result, "2");
}

TEST(ScriptRuntime, ReturnsANumber) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString("int main() { return 40 + 2; }", "test",
                                                  &result));
  EXPECT_EQ(result, "42");
}

TEST(ScriptRuntime, AThrownErrorIsCaughtNotFatal) {
  auto& runtime = ScriptRuntime::Instance();
  EXPECT_FALSE(runtime.RunString(
      "int main() { int[] values; return values[3]; }", "thrower"));
  std::string result;
  EXPECT_TRUE(runtime.RunString("string main() { return \"still alive\"; }", "after", &result));
  EXPECT_EQ(result, "still alive");
}

TEST(ScriptRuntime, ASyntaxErrorFailsTheRun) {
  EXPECT_FALSE(ScriptRuntime::Instance().RunString("int main() { let = = ; }", "bad"));
}

TEST(ScriptRuntime, TheTimeoutAbortsAnInfiniteLoop) {
  auto& runtime = ScriptRuntime::Instance();
  runtime.SetTimeoutSeconds(0.2);
  EXPECT_FALSE(runtime.RunString("int main() { while (true) {} }", "spin"));
  runtime.SetTimeoutSeconds(10.0);
  std::string result;
  EXPECT_TRUE(runtime.RunString("int main() { return 42; }", "after", &result));
  EXPECT_EQ(result, "42");
}
