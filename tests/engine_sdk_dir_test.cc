// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the engine SDK resolver: bundle, next to the executable, next to
// the executable's parent, then the development checkout.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "os/filesystem.h"

namespace fs = std::filesystem;
using namespace Mercury;

namespace {

void MakeSdk(const fs::path& root) {
  fs::create_directories(root / "include" / "mercury");
  fs::create_directories(root / "cmake");
  fs::create_directories(root / "lib");
  std::ofstream(root / "cmake" / "MercuryConfig.cmake") << "# test\n";
}

void MakeDevEngine(const fs::path& engine, const fs::path& build) {
  fs::create_directories(engine / "mercury");
  fs::create_directories(engine / "editor");
  fs::create_directories(engine / "third_party");
  fs::create_directories(engine / "cmake");
  fs::create_directories(build / "generated" / "mercury");
  fs::create_directories(build / "lib");
}

}  // namespace

TEST(EngineSdk, PrefersBundleThenExeThenParentThenDev) {
  const fs::path temp =
      fs::temp_directory_path() /
      ("mercury_sdk_dir_" + std::to_string(std::random_device{}()));
  const fs::path app = temp / "App.app";
  const fs::path exe = app / "Contents" / "MacOS";
  const fs::path bundle = app / "Contents" / "Resources";
  const fs::path engine = temp / "engine";
  const fs::path build = temp / "build";
  fs::create_directories(exe);
  fs::create_directories(bundle);
  MakeDevEngine(engine, build);

  os::EngineSdkPaths paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_TRUE(paths.development);
  EXPECT_EQ(paths.root, engine);
  EXPECT_EQ(paths.build_dir, build);
  // A configured checkout prefers the development package it generated.
  fs::create_directories(build / "dev-sdk");
  std::ofstream(build / "dev-sdk" / "MercuryConfig.cmake") << "# dev\n";
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_EQ(paths.cmake_dir, build / "dev-sdk");
  fs::remove(build / "dev-sdk" / "MercuryConfig.cmake");
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_EQ(paths.cmake_dir, engine / "cmake");
  ASSERT_FALSE(paths.include_dirs.empty());
  EXPECT_EQ(paths.include_dirs[0], engine / "mercury");

  // No SDK next to the exe: Contents/sdk (the exe's parent) wins.
  MakeSdk(app / "Contents" / "sdk");
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_FALSE(paths.development);
  EXPECT_EQ(paths.root, app / "Contents" / "sdk");
  EXPECT_EQ(paths.cmake_dir, paths.root / "cmake");
  EXPECT_EQ(paths.lib_dir, paths.root / "lib");
  EXPECT_EQ(paths.include_dirs[0], paths.root / "include" / "mercury");

  // A directory next to the executable wins over its parent.
  fs::remove_all(app / "Contents" / "sdk");
  MakeSdk(exe / "sdk");
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_EQ(paths.root, exe / "sdk");

  // The .app's Resources/sdk wins over both.
  MakeSdk(bundle / "sdk");
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_EQ(paths.root, bundle / "sdk");
  EXPECT_FALSE(paths.development);

  fs::remove_all(temp);
}

TEST(EngineSdk, RejectsACandidateAnotherAccountCouldChange) {
  const fs::path temp =
      fs::temp_directory_path() /
      ("mercury_sdk_trust_" + std::to_string(std::random_device{}()));
  const fs::path app = temp / "App.app";
  const fs::path exe = app / "Contents" / "MacOS";
  const fs::path bundle = app / "Contents" / "Resources";
  const fs::path engine = temp / "engine";
  const fs::path build = temp / "build";
  fs::create_directories(exe);
  fs::create_directories(bundle);
  MakeDevEngine(engine, build);

  // Next to the executable: writable by every account, so never trusted.
  MakeSdk(exe / "sdk");
  fs::permissions(exe / "sdk", fs::perms::all, fs::perm_options::replace);
  // Its parent still has a trusted SDK, which must win over a bad candidate.
  MakeSdk(app / "Contents" / "sdk");

  os::EngineSdkPaths paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_FALSE(paths.development);
  EXPECT_EQ(paths.root, app / "Contents" / "sdk");
  // The development build tree is still available to the editor.
  EXPECT_EQ(paths.build_dir, build);

  // With no trusted candidate anywhere, the checkout remains the fallback.
  fs::remove_all(app / "Contents" / "sdk");
  paths = os::MakeEngineSdkPaths(exe, bundle, engine, build);
  EXPECT_TRUE(paths.development);
  EXPECT_EQ(paths.root, engine);

  fs::remove_all(temp);
}

TEST(EngineSdk, RunningFromTheBuildTreeUsesTheCheckout) {
  const os::EngineSdkPaths paths = os::EngineSdkDir();
  ASSERT_TRUE(paths.IsValid());
  EXPECT_TRUE(paths.development);
  ASSERT_FALSE(paths.include_dirs.empty());
  EXPECT_TRUE(fs::is_directory(paths.include_dirs[0]));
  const fs::path dev_sdk = fs::path(MERCURY_BUILD_DIR) / "dev-sdk";
  if (fs::exists(dev_sdk / "MercuryConfig.cmake")) {
    EXPECT_EQ(paths.cmake_dir, dev_sdk);
  } else {
    EXPECT_EQ(paths.cmake_dir, fs::path(MERCURY_ENGINE_DIR) / "cmake");
  }
}
