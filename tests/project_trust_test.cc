// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the workspace-trust gate: a C++ project is never configured by a
// build that was not preceded by a trust decision, granting trust clears
// pre-existing build output (without following links), and runtime GUIDs do
// not collide with map uids.

#include "project_trust.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "console.h"
#include "editor_settings.h"
#include "play_session.h"
#include "pobject/manager.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace Mercury;

namespace {

fs::path TempDir(const char* name) {
  const fs::path dir =
      fs::temp_directory_path() /
      (std::string(name) + "_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  return dir;
}

class XdgConfigScope {
 public:
  explicit XdgConfigScope(const fs::path& dir) {
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    had_previous_ = previous != nullptr;
    if (previous) previous_ = previous;
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
  }
  ~XdgConfigScope() {
    if (had_previous_) {
      setenv("XDG_CONFIG_HOME", previous_.c_str(), 1);
    } else {
      unsetenv("XDG_CONFIG_HOME");
    }
  }

 private:
  std::string previous_;
  bool had_previous_ = false;
};

}  // namespace

TEST(ProjectTrust, FirstGrantClearsShippedBuildOutputWithoutFollowingLinks) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test uses symlinks and XDG_CONFIG_HOME";
#else
  const fs::path config = TempDir("mercury_trust_config");
  const fs::path root = TempDir("mercury_trust_project");
  XdgConfigScope scope(config);

  const fs::path build = root / ".mercury" / "build";
  const fs::path systems = root / ".mercury" / "systems";
  fs::create_directories(build);
  fs::create_directories(systems);
  std::ofstream(build / "CMakeCache.txt") << "// shipped by someone else\n";
  std::ofstream(build / "build.ninja") << "rule evil\n";
  std::ofstream(systems / "old.so") << "old";

  const fs::path outside = root / "outside.txt";
  std::ofstream(outside) << "keep me\n";
  fs::create_symlink(outside, systems / "link.so");

  Console console;
  EditorSettings settings;
  ASSERT_FALSE(settings.IsProjectTrusted(root));
  EditorUI::SetProjectTrust(settings, root, true, console);
  EXPECT_TRUE(settings.IsProjectTrusted(root));
  EXPECT_FALSE(fs::exists(build));
  EXPECT_FALSE(fs::exists(systems));
  // The link was removed as a link; its target was untouched.
  EXPECT_TRUE(fs::is_regular_file(outside));
  EXPECT_EQ(std::ifstream(outside).get(), 'k');

  // Only the first grant clears; a later confirmation leaves the new build.
  fs::create_directories(build);
  std::ofstream(build / "CMakeCache.txt") << "// ours\n";
  EditorUI::SetProjectTrust(settings, root, true, console);
  EXPECT_TRUE(fs::exists(build / "CMakeCache.txt"));

  fs::remove_all(root);
  fs::remove_all(config);
#endif
}

TEST(ProjectTrust, BuildDoesNotRunForAnUntrustedProject) {
  const fs::path root = TempDir("mercury_untrusted_play");
  auto project = Project::Create(root / "game.mproj", "Game");
  ASSERT_TRUE(project);
  ASSERT_TRUE(project->SetBuildTarget("game"));
  ASSERT_TRUE(project->Save());

  EditorSettings settings;  // no trust decision for this project
  Console console;
  PlaySession session(console, settings);
  session.Build(*project);
  session.Update();
  EXPECT_FALSE(session.IsBusy());
  EXPECT_FALSE(
      fs::exists(root / ".mercury" / "build" / "debug" / "CMakeCache.txt"));
  fs::remove_all(root);
}

TEST(ProjectTrust, RuntimeGuidsDoNotCollideWithMapUids) {
  PObjectManager objects;
  // GameScene creates its camera first; this used to take guid 1 and shadow
  // the first map node's uid.
  const kEntityId camera = objects.NewObject();
  const kEntityId first = objects.NewObject();
  const kEntityId second = objects.NewObject();
  ASSERT_TRUE(objects.SetGuid(first, 1));
  ASSERT_TRUE(objects.SetGuid(second, 2));
  EXPECT_EQ(objects.GetGuid(first), 1u);
  EXPECT_EQ(objects.GetGuid(second), 2u);
  EXPECT_EQ(objects.FindByGuid(1), first);
  EXPECT_EQ(objects.FindByGuid(2), second);
  EXPECT_NE(objects.GetGuid(camera), 1u);
  EXPECT_NE(objects.GetGuid(camera), 2u);
  EXPECT_EQ(objects.FindByGuid(objects.GetGuid(camera)), camera);
}
