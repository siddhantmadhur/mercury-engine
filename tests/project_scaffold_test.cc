// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the per-project generated files: CMakeLists.txt/CMakePresets.json
// only when missing, .clangd refreshed from the running engine, and the
// compile_commands link.

#include "project_scaffold.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "cmake_build.h"
#include "os/filesystem.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace Mercury;

namespace {

std::string Read(const fs::path& file) {
  std::ifstream in(file);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

fs::path MakeProject() {
  const fs::path root =
      fs::temp_directory_path() /
      ("mercury_scaffold_" + std::to_string(std::random_device{}()));
  fs::create_directories(root);
  auto project = Project::Create(root / "scaffold.mproj", "Scaffold Test");
  EXPECT_TRUE(project->Save());
  return root;
}

}  // namespace

TEST(ProjectScaffold, WritesBuildFilesAndClangd) {
  const fs::path root = MakeProject();
  auto project = Project::Load(root / "scaffold.mproj");
  ASSERT_TRUE(project);

  EditorUI::ScaffoldProject(*project);

  const std::string cmake = Read(root / "CMakeLists.txt");
  EXPECT_NE(cmake.find("mercury_add_game(scaffold_test"), std::string::npos)
      << cmake;
  EXPECT_NE(cmake.find("mercury_add_editor_system(scaffold_test_systems"),
            std::string::npos)
      << cmake;

  const std::string presets = Read(root / "CMakePresets.json");
  EXPECT_NE(presets.find("$env{MERCURY_SDK}/cmake"), std::string::npos)
      << presets;
  // The generated presets must not bake in this machine's SDK path.
  EXPECT_EQ(presets.find(os::EngineSdkDir().root.string()), std::string::npos);

  const std::string clangd = Read(root / ".clangd");
  EXPECT_NE(clangd.find("CompilationDatabase: .mercury/build/debug"),
            std::string::npos)
      << clangd;
  EXPECT_NE(clangd.find("-std=c++20"), std::string::npos);
  for (const fs::path& dir : os::EngineSdkDir().include_dirs) {
    EXPECT_NE(clangd.find(dir.string()), std::string::npos)
        << "missing " << dir << " in .clangd";
  }

  // A user's CMakeLists is never replaced, while .clangd is always refreshed.
  std::ofstream(root / "CMakeLists.txt") << "# mine\n";
  EditorUI::ScaffoldProject(*project);
  EXPECT_EQ(Read(root / "CMakeLists.txt"), "# mine\n");

  fs::remove_all(root);
}

TEST(ProjectBuildLock, SerializesProjectBuilds) {
  const fs::path first = fs::temp_directory_path() / "mercury_build_a";
  const fs::path second = fs::temp_directory_path() / "mercury_build_b";
  ASSERT_TRUE(cmake_build::TryAcquireProjectBuild(first));
  EXPECT_TRUE(cmake_build::IsProjectBuildBusy());
  EXPECT_FALSE(cmake_build::TryAcquireProjectBuild(second));
  cmake_build::ReleaseProjectBuild(first);
  EXPECT_FALSE(cmake_build::IsProjectBuildBusy());
  // Releasing a project that does not hold the lock is a no-op.
  cmake_build::ReleaseProjectBuild(second);
  ASSERT_TRUE(cmake_build::TryAcquireProjectBuild(second));
  cmake_build::ReleaseProjectBuild(second);
  EXPECT_FALSE(cmake_build::IsProjectBuildBusy());
}

TEST(ProjectScaffold, LinksOnlyADatabaseThisEditorConfigured) {
  const fs::path root = MakeProject();
  auto project = Project::Load(root / "scaffold.mproj");
  ASSERT_TRUE(project);
  const fs::path build = root / ".mercury" / "build" / "debug";
  const fs::path db = build / "compile_commands.json";
  fs::create_directories(build);
  std::ofstream(db) << "[]\n";

  const fs::path link = root / "compile_commands.json";
  // Shipped build output has no editor marker: never expose it to clangd.
  EditorUI::ScaffoldProject(*project);
  EXPECT_FALSE(fs::exists(link));

  // A database written after this editor configured the tree is linked.
  fs::remove_all(root / ".mercury" / "build");
  std::string error;
  ASSERT_FALSE(os::EnsureProjectBuildDir(root, "debug", &error).empty())
      << error;
  std::ofstream(db) << "[]\n";
  EditorUI::ScaffoldProject(*project);
  ASSERT_TRUE(fs::exists(link));
  EXPECT_EQ(Read(link), "[]\n");
#if !defined(_WIN32)
  EXPECT_TRUE(fs::is_symlink(link));
#endif

  fs::remove_all(root);
}

TEST(ProjectScaffold, NeverInterpolatesAMalformedTarget) {
  const fs::path root = MakeProject();
  auto project = Project::Load(root / "scaffold.mproj");
  ASSERT_TRUE(project);

  // A document edited in memory (Project::Load would reject this file) must
  // not reach the generated CMakeLists.
  project->GetDocument().Add("build").properties.Set(
      "target", "game)\nexecute_process(COMMAND echo pwned)\n#");
  ASSERT_FALSE(fs::exists(root / "CMakeLists.txt"));
  EditorUI::ScaffoldProject(*project);
  EXPECT_FALSE(fs::exists(root / "CMakeLists.txt"));
  // The LSP files still describe the project safely.
  EXPECT_TRUE(fs::exists(root / ".clangd"));
  EXPECT_TRUE(fs::exists(root / "CMakePresets.json"));

  fs::remove_all(root);
}
