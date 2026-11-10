// os::UserPath: command-line paths under `bazel run`, which starts programs in the runfiles tree.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "os/filesystem.h"
#include "test_paths.h"

namespace fs = std::filesystem;

namespace {

void SetEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  if (value.empty()) {
    unsetenv(name);
  } else {
    setenv(name, value.c_str(), 1);
  }
#endif
}

class UserPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = Mercury::test::CacheDir() / "user_path";
    std::error_code ec;
    fs::remove_all(dir_, ec);
    fs::create_directories(dir_ / "examples", ec);
    std::ofstream(dir_ / "examples" / "game.mproj") << "x";
  }
  void TearDown() override { SetEnv("BUILD_WORKING_DIRECTORY", ""); }
  fs::path dir_;
};

}  // namespace

TEST_F(UserPathTest, RelativePathIsTriedFromTheBazelRunDirectory) {
  SetEnv("BUILD_WORKING_DIRECTORY", dir_.string());
  EXPECT_EQ(Mercury::os::UserPath("examples/game.mproj"), dir_ / "examples" / "game.mproj");
}

TEST_F(UserPathTest, OtherPathsComeBackAsGiven) {
  SetEnv("BUILD_WORKING_DIRECTORY", dir_.string());
  // Not there either: left alone, so the error names what the user typed.
  EXPECT_EQ(Mercury::os::UserPath("examples/none.mproj"), fs::path("examples/none.mproj"));
  // Absolute paths and empty ones are never rewritten.
  EXPECT_EQ(Mercury::os::UserPath(dir_ / "examples" / "game.mproj"),
            dir_ / "examples" / "game.mproj");
  EXPECT_EQ(Mercury::os::UserPath(""), fs::path());
  // Without the variable nothing changes.
  SetEnv("BUILD_WORKING_DIRECTORY", "");
  EXPECT_EQ(Mercury::os::UserPath("examples/game.mproj"), fs::path("examples/game.mproj"));
}
