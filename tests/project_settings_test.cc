// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the build settings the editor derives from a project.

#include "project/project.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "serialization/mtext.h"

using namespace Mercury;

TEST(ProjectSettings, BuildSystemsTargetDefaultsToTheBuildTargetSuffix) {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("mercury_project_targets_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  const auto file = dir / "test.mproj";
  {
    std::ofstream out(file);
    out << "[project]\nname = \"Targets\"\n\n[build]\ntarget = \"game\"\n";
  }

  std::string error;
  auto project = Project::Load(file, &error);
  ASSERT_NE(project, nullptr) << error;
  EXPECT_EQ(project->GetBuildSystemsTarget(), "game_systems");

  // An explicit [build] systems_target wins.
  project->GetDocument().Find("build")->properties.Set("systems_target",
                                                       "my_tools");
  EXPECT_EQ(project->GetBuildSystemsTarget(), "my_tools");

  // Without a build target there is no default systems target.
  auto anascript = dir / "as.mproj";
  {
    std::ofstream out(anascript);
    out << "[project]\nname = \"As\"\n";
  }
  auto as_project = Project::Load(anascript, &error);
  ASSERT_NE(as_project, nullptr) << error;
  EXPECT_EQ(as_project->GetBuildSystemsTarget(), "");

  std::filesystem::remove_all(dir);
}
