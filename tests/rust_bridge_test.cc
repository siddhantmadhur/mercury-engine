#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "os/filesystem.rs.h"
#include "rust/bridge.h"
#include "test_paths.h"

namespace fs = std::filesystem;

namespace {

TEST(RustBridgeTest, InitializesAndReportsVersion) {
  EXPECT_TRUE(Mercury::rust::Init());
  EXPECT_TRUE(Mercury::rust::IsEnabled());
  const ::rust::Str version = Mercury::rust::Version();
  EXPECT_FALSE(version.empty());
  EXPECT_EQ(std::string(version), "dev");
}

TEST(RustBridgeTest, RoundTripsThroughCxxBridge) {
  const ::rust::String result = Mercury::rust::RoundTripSelfTest("hello");
  EXPECT_EQ(std::string(result), "rust:hello:engine=dev");
}

TEST(RustBridgeTest, RoutesRustLogsToPDebug) {
  struct SinkGuard {
    ~SinkGuard() { PDebug::SetSink({}); }
  } guard;

  std::vector<std::pair<PDebug::Level, std::string>> captured;
  PDebug::SetSink([&](PDebug::Level level, const std::string& message) {
    captured.emplace_back(level, message);
  });

  Mercury::rust::LogInfoFromRust("rust-bridge-log-check");
  ASSERT_EQ(captured.size(), 1u);
  EXPECT_EQ(captured[0].first, PDebug::Level::kInfo);
  EXPECT_EQ(captured[0].second, "rust-bridge-log-check");
}

TEST(RustBridgeTest, ReadsVfsFilesFromRust) {
  const fs::path dir = Mercury::test::CacheDir() / "rust_bridge_fs";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);

  const fs::path probe = dir / "probe.txt";
  {
    std::ofstream out(probe);
    out << "hello from mercury vfs";
  }

  const ::rust::String text = Mercury::rust::ReadTextFromRust(probe.string());
  EXPECT_EQ(std::string(text), "hello from mercury vfs");

  EXPECT_THROW(Mercury::rust::ReadTextFromRust((dir / "missing.txt").string()),
               ::rust::Error);

  fs::remove_all(dir, ec);
}

}  // namespace
