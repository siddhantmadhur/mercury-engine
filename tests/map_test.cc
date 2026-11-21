// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "project/map.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "project/asset_database.h"
#include "project/project.h"
#include "scene/map_scene.h"

using namespace Mercury;

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

const std::filesystem::path kPong =
    std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/pong";

}  // namespace

// Every map and prefab in the repo must be a fixed point of load -> save:
// the files were written by SaveToString, so re-saving them reproduces them
// byte for byte. Renumber files that fail here (write the engine's own
// SaveToString output) instead of changing the writer.
TEST(Map, EveryRepoMapRoundTripsByteForByte) {
  const std::filesystem::path root = MERCURY_SOURCE_DIR;
  int checked = 0;
  for (auto it =
           std::filesystem::recursive_directory_iterator(root / "examples");
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (!it->is_regular_file()) continue;
    const std::string ext = it->path().extension().string();
    if (ext != ".mmap" && ext != ".mprefab") continue;
    SCOPED_TRACE(it->path().string());
    const std::string original = ReadFile(it->path());
    ASSERT_FALSE(original.empty());
    Map map;
    std::string error;
    ASSERT_TRUE(map.LoadFromString(original, &error)) << error;
    EXPECT_EQ(map.SaveToString(), original);
    checked++;
  }
  EXPECT_GE(checked, 6) << "expected the six example maps";
}

TEST(Map, PongMapRoundTripsByteForByte) {
  const auto path = kPong / "maps/main.mmap";
  const std::string original = ReadFile(path);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(original, &error)) << error;
  EXPECT_EQ(map.SaveToString(), original);
}

TEST(Map, RollABallMapRoundTripsByteForByte) {
  const auto path = std::filesystem::path(MERCURY_SOURCE_DIR) /
                    "examples/roll_a_ball/maps/main.mmap";
  const std::string original = ReadFile(path);
  ASSERT_FALSE(original.empty());
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(original, &error)) << error;
  EXPECT_EQ(map.SaveToString(), original);
}

TEST(Map, MonkeyBallMapRoundTripsByteForByte) {
  const auto path = std::filesystem::path(MERCURY_SOURCE_DIR) /
                    "examples/monkey_ball/maps/main.mmap";
  const std::string original = ReadFile(path);
  ASSERT_FALSE(original.empty());
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(original, &error)) << error;
  EXPECT_EQ(map.SaveToString(), original);
}

TEST(Project, ReadsGravity) {
  std::string error;
  auto project = Project::Load(std::filesystem::path(MERCURY_SOURCE_DIR) /
                                   "examples/roll_a_ball/roll_a_ball.mproj",
                               &error);
  ASSERT_TRUE(project) << error;
  EXPECT_FLOAT_EQ(project->GetGravity().y, -9.81f);
  // Projects without a [physics] section get Earth gravity too.
  auto pong = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(pong) << error;
  EXPECT_FLOAT_EQ(pong->GetGravity().y, -9.81f);
}

TEST(Map, PongMapStructure) {
  Map map;
  std::string error;
  ASSERT_TRUE(map.Load(kPong / "maps/main.mmap", &error)) << error;
  EXPECT_EQ(map.GetRoot()->name, "Pong");
  auto ball = map.FindNode("Ball");
  ASSERT_NE(ball, nullptr);
  auto sprite = ball->FindComponent("SpriteRenderer");
  ASSERT_NE(sprite, nullptr);
  // ExtResource ids become res:// paths in memory.
  EXPECT_EQ(sprite->properties.Get("material")->Get<mtext::Resource>()->path,
            "res://assets/white.masset");
  auto left = map.FindNode("Walls/Left");
  ASSERT_NE(left, nullptr);
  EXPECT_EQ(left->GetPath(), "Walls/Left");
  EXPECT_EQ(left->parent, map.FindNode("Walls"));
}

TEST(Map, RejectsBrokenFiles) {
  Map map;
  std::string error;
  EXPECT_FALSE(map.LoadFromString("[[node]]\nname = \"A\"\n", &error));
  EXPECT_FALSE(
      map.LoadFromString("[mmap]\nformat = 2\n\n[[node]]\n"
                         "name = \"A\"\n\n[[node]]\n"
                         "name = \"B\"\nparent = \"X\"\n",
                         &error));
  EXPECT_NE(error.find("parent"), std::string::npos);
  EXPECT_FALSE(
      map.LoadFromString("[mmap]\nformat = 2\n\n[[node]]\n"
                         "name = \"A\"\n\n[[component]]\n"
                         "node = \".\"\ntype = \"T\"\n\n"
                         "[component.props]\nm = { ext = \"9\" }\n",
                         &error));
  EXPECT_NE(error.find("not declared"), std::string::npos);
}

TEST(Map, EditingKeepsNamesUniqueAndUpdatesNodePaths) {
  Map map;
  auto walls = map.AddNode(map.GetRoot(), "Walls");
  auto left = map.AddNode(walls, "Left");
  auto again = map.AddNode(walls, "Left");
  EXPECT_EQ(again->name, "Left2");

  auto game = map.AddNode(map.GetRoot(), "Game");
  game->components.push_back({"Goal", {}});
  game->components[0].properties.Set("wall", mtext::NodePath{"Walls/Left"});
  game->components[0].properties.Set("group", mtext::NodePath{"Walls"});

  map.RenameNode(walls, "Edges");
  EXPECT_EQ(
      game->components[0].properties.Get("wall")->Get<mtext::NodePath>()->path,
      "Edges/Left");
  EXPECT_EQ(
      game->components[0].properties.Get("group")->Get<mtext::NodePath>()->path,
      "Edges");

  ASSERT_TRUE(map.ReparentNode(left, map.GetRoot()));
  EXPECT_EQ(left->GetPath(), "Left");
  EXPECT_EQ(
      game->components[0].properties.Get("wall")->Get<mtext::NodePath>()->path,
      "Left");
  EXPECT_FALSE(map.ReparentNode(walls, walls));

  auto copy = map.DuplicateNode(game);
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ(copy->name, "Game2");
  EXPECT_NE(copy->uid, game->uid);
  EXPECT_EQ(copy->IndexInParent(), game->IndexInParent() + 1);

  EXPECT_TRUE(map.RemoveNode(walls));
  EXPECT_EQ(map.FindNode("Edges"), nullptr);
  EXPECT_FALSE(map.RemoveNode(map.GetRoot()));

  // A saved and reloaded copy has the same structure.
  Map reloaded;
  std::string error;
  ASSERT_TRUE(reloaded.LoadFromString(map.SaveToString(), &error)) << error;
  EXPECT_EQ(reloaded.SaveToString(), map.SaveToString());
  EXPECT_EQ(reloaded.NodeCount(), map.NodeCount());
}

TEST(Project, ResolvesResPaths) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;
  EXPECT_EQ(project->GetName(), "Pong");
  EXPECT_EQ(project->GetMainMap(), "res://maps/main.mmap");
  EXPECT_EQ(project->GetWindowWidth(), 1280);
  const auto map_file = project->Resolve(project->GetMainMap());
  EXPECT_TRUE(std::filesystem::exists(map_file));
  EXPECT_EQ(project->ToResPath(map_file), "res://maps/main.mmap");
  EXPECT_EQ(project->ToResPath("/definitely/outside"), "");
}

TEST(Project, ResolveStaysInsideProject) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;
  EXPECT_FALSE(project->Resolve("res://maps/main.mmap").empty());
  // New files inside the project are fine, existing or not.
  EXPECT_FALSE(project->Resolve("res://maps/not_yet_saved.mmap").empty());
  EXPECT_TRUE(project->Resolve("res://../../../../etc/passwd").empty());
  EXPECT_TRUE(project->Resolve("../../../etc/passwd").empty());
  EXPECT_TRUE(project->Resolve("res:///etc/passwd").empty());
  EXPECT_TRUE(project->Resolve("/etc/passwd").empty());
  EXPECT_TRUE(project->Resolve("").empty());
  // An absolute path that points back inside the project is accepted.
  EXPECT_FALSE(
      project->Resolve((project->GetRoot() / "maps/main.mmap").string())
          .empty());
}

TEST(MapScene, CurrentCameraSetsSkyboxPath) {
  std::string error;
  auto project = Project::Load(kPong / "pong.mproj", &error);
  ASSERT_TRUE(project) << error;

  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "Camera"
type = "Transform"

[[component]]
node = "Camera"
type = "Camera"

[component.props]
skybox = { ext = "1" }

[[ext_resource]]
id = "1"
path = "res://assets/sky.png"
type = "Texture"

[[node]]
name = "Root"

[[node]]
name = "Camera"
parent = "."
)",
                                 &error))
      << error;

  MapScene scene(project, MapScene::Mode::kEdit);
  scene.Instantiate(map);
  EXPECT_EQ(scene.GetSkyboxPath(), project->Resolve("res://assets/sky.png"));

  // Without a skybox the renderer falls back to the background color.
  scene.Instantiate(Map());
  EXPECT_EQ(scene.GetSkyboxPath(), "");
}

// Load() hands out a snapshot: reloading a changed file must not rewrite the
// Asset a caller is still holding.
TEST(AssetDatabase, ReloadLeavesEarlierSnapshotsUntouched) {
  namespace fs = std::filesystem;
  const fs::path dir =
      fs::temp_directory_path() /
      ("asset_db_test_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir / "assets");
  {
    std::ofstream(dir / "t.mproj")
        << "[project]\nformat = 2\n\n[project.props]\nname = \"t\"\n";
  }
  const auto write = [&](const char* value) {
    std::ofstream(dir / "assets/a.masset")
        << "[masset]\nformat = 2\n\ntype = \"Thing\"\n\n[masset.props]\n"
           "value = \""
        << value << "\"\n";
  };
  write("first");
  std::string error;
  auto project = Project::Load(dir / "t.mproj", &error);
  ASSERT_TRUE(project) << error;
  AssetDatabase assets(project);

  auto before = assets.Load("res://assets/a.masset");
  ASSERT_TRUE(before);
  EXPECT_EQ(assets.Load("res://assets/a.masset"), before);  // cached
  EXPECT_EQ(*before->properties.Get("value")->ToString(), "first");

  write("second");
  fs::last_write_time(
      dir / "assets/a.masset",
      fs::last_write_time(dir / "assets/a.masset") + std::chrono::seconds(5));
  auto after = assets.Load("res://assets/a.masset");
  ASSERT_TRUE(after);
  EXPECT_NE(after, before);
  EXPECT_EQ(*after->properties.Get("value")->ToString(), "second");
  // The first snapshot is exactly as it was.
  EXPECT_EQ(*before->properties.Get("value")->ToString(), "first");

  // A path outside the project is refused rather than read.
  EXPECT_FALSE(assets.Load("res://../../../etc/hosts"));
  fs::remove_all(dir);
}
