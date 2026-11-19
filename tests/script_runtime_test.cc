// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the embedded AngelScript engine itself: evaluation, exceptions and
// the infinite-loop guard.
#include "scripting/as/script_runtime.h"
#include "scripting/as/script_builder.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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
  ASSERT_TRUE(ScriptRuntime::Instance().RunString(
      "string main() { return \"hello\"; }", "test", &result));
  EXPECT_EQ(result, "hello");
}

TEST(ScriptRuntime, ConstructsAValueType) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString(
      "string main() { Vector3 v(1.0f, 2.0f, 3.0f); return \"\" + v.y; }",
      "vec", &result));
  EXPECT_EQ(result, "2");
}

TEST(ScriptRuntime, ReturnsANumber) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString(
      "int main() { return 40 + 2; }", "test", &result));
  EXPECT_EQ(result, "42");
}

TEST(ScriptRuntime, ANegativeNumberKeepsItsSign) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString(
      "int main() { return -1; }", "negative", &result));
  EXPECT_EQ(result, "-1");
}

// A rebuild that fails must leave the working scripts alone: the module that
// behaviours are using used to be discarded as soon as the build started.
TEST(ScriptRuntime, AFailedRebuildKeepsTheCurrentModule) {
  auto& runtime = ScriptRuntime::Instance();
  const auto dir = std::filesystem::temp_directory_path() /
                   ("rebuild_test_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  const auto good = dir / "good.as";
  const auto bad = dir / "bad.as";
  std::ofstream(good) << "class Good { int Value() { return 7; } }";
  std::ofstream(bad) << "class Broken { void f( }";

  // A working module first, so there is something to lose.
  ASSERT_TRUE(runtime.LoadFiles({good}, "game", true));
  asIScriptModule* before = runtime.GetModule();
  ASSERT_NE(before, nullptr);
  ASSERT_NE(before->GetTypeInfoByName("Good"), nullptr);
  const std::string name = before->GetName();

  EXPECT_FALSE(runtime.LoadFiles({bad}, "game", true));

  // Still the current module, still registered with the engine under its own
  // name (not discarded by the failed build), and its class still there.
  EXPECT_EQ(runtime.GetModule(), before);
  EXPECT_EQ(runtime.GetEngine()->GetModule(name.c_str(), asGM_ONLY_IF_EXISTS),
            before);
  EXPECT_NE(before->GetTypeInfoByName("Good"), nullptr);

  // And a later good build still works (a fresh module name each time).
  EXPECT_TRUE(runtime.LoadFiles({good}, "game", true));
  EXPECT_NE(runtime.GetModule(), nullptr);
  std::filesystem::remove_all(dir);
}

// A second build of the same project must look at the files as they are now:
// it used to reuse the first build's module and report success for scripts
// that no longer compile.
TEST(ScriptRuntime, BuildingAgainChecksTheScriptsAgain) {
  const auto dir = std::filesystem::temp_directory_path() /
                   ("rebuild_project_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir / "scripts");
  const auto script = dir / "scripts" / "game.as";
  std::ofstream(script) << "class Thing { int Value() { return 1; } }";

  BuildOptions options;
  std::string log;
  EXPECT_TRUE(ScriptBuilder::Build(dir, options, &log)) << log;

  std::ofstream(script) << "class Thing { int Value() { return ; } }";
  log.clear();
  EXPECT_FALSE(ScriptBuilder::Build(dir, options, &log)) << log;
  std::filesystem::remove_all(dir);
}

TEST(ScriptRuntime, AThrownErrorIsCaughtNotFatal) {
  auto& runtime = ScriptRuntime::Instance();
  EXPECT_FALSE(runtime.RunString(
      "int main() { int[] values; return values[3]; }", "thrower"));
  std::string result;
  EXPECT_TRUE(runtime.RunString("string main() { return \"still alive\"; }",
                                "after", &result));
  EXPECT_EQ(result, "still alive");
}

TEST(ScriptRuntime, ASyntaxErrorFailsTheRun) {
  EXPECT_FALSE(
      ScriptRuntime::Instance().RunString("int main() { let = = ; }", "bad"));
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
