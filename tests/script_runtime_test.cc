// Tests of the embedded JavaScript engine: evaluation, error handling, promises, the watchdog
// and source maps.
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <sstream>

#include "scripting/js/script_runtime.h"
#include "scripting/js/source_map.h"

using namespace Mercury;

namespace {

// V8 starts once for the whole test run.
class ScriptRuntimeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { ASSERT_TRUE(ScriptRuntime::Instance().Initialize()); }
  void TearDown() override { ScriptRuntime::Instance().Shutdown(); }
};

[[maybe_unused]] const auto* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new ScriptRuntimeEnvironment);

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

TEST(ScriptRuntime, EvaluatesAString) {
  std::string result;
  ASSERT_TRUE(ScriptRuntime::Instance().RunString("1 + 1", "test", &result));
  EXPECT_EQ(result, "2");
}

TEST(ScriptRuntime, AThrownErrorIsCaughtNotFatal) {
  auto& runtime = ScriptRuntime::Instance();
  EXPECT_FALSE(runtime.RunString("throw new Error('boom')", "thrower"));
  std::string result;
  EXPECT_TRUE(runtime.RunString("'still alive'", "after", &result));
  EXPECT_EQ(result, "still alive");
}

TEST(ScriptRuntime, ASyntaxErrorFailsTheRun) {
  EXPECT_FALSE(ScriptRuntime::Instance().RunString("let = = ;", "bad"));
}

TEST(ScriptRuntime, PromiseCallbacksRunAfterTheCall) {
  auto& runtime = ScriptRuntime::Instance();
  ASSERT_TRUE(runtime.RunString("globalThis.promised = 0; Promise.resolve().then(() => { globalThis.promised = 5; });"));
  std::string result;
  ASSERT_TRUE(runtime.RunString("globalThis.promised", "read", &result));
  EXPECT_EQ(result, "5");
}

TEST(ScriptRuntime, ConsoleIsAvailable) {
  EXPECT_TRUE(ScriptRuntime::Instance().RunString("console.log('hello from a test', {a: 1})"));
}

TEST(ScriptRuntime, TheWatchdogEndsAnInfiniteLoop) {
  auto& runtime = ScriptRuntime::Instance();
  runtime.SetTimeoutSeconds(1.0);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(runtime.RunString("while (true) {}", "spin"));
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  EXPECT_LT(elapsed, 5.0);
  // The next call is not affected by the termination.
  std::string result;
  EXPECT_TRUE(runtime.RunString("40 + 2", "after", &result));
  EXPECT_EQ(result, "42");
  runtime.SetTimeoutSeconds(10.0);
}

TEST(SourceMapTest, MapsABundlePositionToTypeScript) {
  SourceMap map;
  std::string error;
  ASSERT_TRUE(map.Parse(ReadFile(std::string(MERCURY_SOURCE_DIR) + "/tests/scripts/fixture.js.map"), &error)) << error;
  // Line 13 of fixture.js is `if (step > 100) {`, line 5 of scripts/ball.ts.
  auto position = map.Lookup(13, 9);
  ASSERT_TRUE(position.has_value());
  EXPECT_EQ(position->file, "scripts/ball.ts");
  EXPECT_EQ(position->line, 5);
  EXPECT_EQ(position->column, 5);
  // `throw new Error` on line 14 is line 6.
  auto thrown = map.Lookup(14, 11);
  ASSERT_TRUE(thrown.has_value());
  EXPECT_EQ(thrown->line, 6);
}

TEST(SourceMapTest, RejectsGarbage) {
  SourceMap map;
  EXPECT_FALSE(map.Parse("not json"));
  EXPECT_FALSE(map.Parse("{\"version\": 2}"));
}
