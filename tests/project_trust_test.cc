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
#include "os/filesystem.h"
#include "play_session.h"
#include "pobject/manager.h"
#include "project/project.h"
#include "project_scaffold.h"
#include "scripting/as/script_builder.h"

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

TEST(ProjectTrust, SymlinkedCacheRefusesTrustAndWritesNothingOutside) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test uses symlinks";
#else
  const fs::path config = TempDir("mercury_trust_link_config");
  const fs::path root = TempDir("mercury_trust_link_project");
  const fs::path outside = TempDir("mercury_trust_link_outside");
  XdgConfigScope scope(config);

  fs::create_symlink(outside, root / ".mercury");

  std::string error;
  EXPECT_TRUE(os::EnsureProjectCacheDir(root, &error).empty());
  EXPECT_FALSE(error.empty());

  Console console;
  EditorSettings settings;
  EditorUI::SetProjectTrust(settings, root, true, console);
  EXPECT_FALSE(settings.IsProjectTrusted(root));
  // Nothing was written into the link's target.
  EXPECT_TRUE(fs::is_empty(outside));

  // A Play build stops at the same check, even if trust was set externally.
  settings.SetProjectTrusted(root, true);
  settings.Save();
  auto project = Project::Create(root / "game.mproj", "Game");
  ASSERT_TRUE(project);
  ASSERT_TRUE(project->SetBuildTarget("game"));
  ASSERT_TRUE(project->Save());
  PlaySession session(console, settings);
  session.Build(*project);
  EXPECT_FALSE(session.IsBusy());
  EXPECT_TRUE(fs::is_empty(outside));

  // The LSP files and script-build stamp use the same guarded cache.
  EditorUI::ScaffoldProject(*project);
  EXPECT_TRUE(PlaySession::SchemaPath(*project).empty());
  EXPECT_TRUE(ScriptBuilder::StampPath(root).empty());
  EXPECT_TRUE(fs::is_empty(outside));

  fs::remove_all(root);
  fs::remove_all(config);
  fs::remove_all(outside);
#endif
}

TEST(ProjectCache, BuildDirsNeedTheEditorMarker) {
  const fs::path root = TempDir("mercury_build_marker");
  std::string error;

  const fs::path first = os::EnsureProjectBuildDir(root, "debug", &error);
  ASSERT_FALSE(first.empty()) << error;
  EXPECT_TRUE(os::IsEditorProjectBuildDir(first));
  EXPECT_EQ(first, root / ".mercury" / "build" / "debug");
  // A second call accepts our own marker.
  EXPECT_FALSE(os::EnsureProjectBuildDir(root, "debug", &error).empty());

  // Build output shipped without the marker is refused.
  const fs::path ships = TempDir("mercury_build_shipped");
  fs::create_directories(ships / ".mercury" / "build" / "debug");
  std::ofstream(ships / ".mercury" / "build" / "debug" / "CMakeCache.txt")
      << "// not ours\n";
  error.clear();
  EXPECT_TRUE(os::EnsureProjectBuildDir(ships, "debug", &error).empty());
  EXPECT_NE(error.find("did not create"), std::string::npos) << error;

  // A symlinked build folder is refused too.
  const fs::path linked = TempDir("mercury_build_linked");
  const fs::path target = TempDir("mercury_build_target");
  fs::create_directories(linked / ".mercury");
  fs::create_symlink(target, linked / ".mercury" / "build");
  error.clear();
  EXPECT_TRUE(os::EnsureProjectBuildDir(linked, "debug", &error).empty());
  EXPECT_TRUE(fs::is_empty(target));

  fs::remove_all(root);
  fs::remove_all(ships);
  fs::remove_all(linked);
  fs::remove_all(target);
}

TEST(ProjectTrust, FailedWipeDoesNotPersistTrust) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes POSIX permissions";
#else
  const fs::path config = TempDir("mercury_trust_fail_config");
  const fs::path root = TempDir("mercury_trust_fail_project");
  XdgConfigScope scope(config);
  fs::create_directories(root / ".mercury" / "build");
  std::ofstream(root / ".mercury" / "build" / "CMakeCache.txt") << "// x\n";
  fs::permissions(root / ".mercury",
                  fs::perms::owner_read | fs::perms::owner_exec,
                  fs::perm_options::replace);

  Console console;
  EditorSettings settings;
  EditorUI::SetProjectTrust(settings, root, true, console);
  EXPECT_FALSE(settings.IsProjectTrusted(root));

  fs::permissions(root / ".mercury", fs::perms::owner_all,
                  fs::perm_options::replace);
  fs::remove_all(root);
  fs::remove_all(config);
#endif
}
