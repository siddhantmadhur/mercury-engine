#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "project/map.h"
#include "project/project.h"

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

TEST(Map, PongMapRoundTripsByteForByte) {
  const auto path = kPong / "maps/main.mmap";
  const std::string original = ReadFile(path);
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(original, &error)) << error;
  EXPECT_EQ(map.SaveToString(), original);
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
  EXPECT_FALSE(map.LoadFromString("[node name=\"A\"]\n", &error));
  EXPECT_FALSE(map.LoadFromString(
      "[mmap format=1]\n\n[node name=\"A\"]\n\n[node name=\"B\" parent=\"X\"]\n",
      &error));
  EXPECT_NE(error.find("parent"), std::string::npos);
  EXPECT_FALSE(map.LoadFromString(
      "[mmap format=1]\n\n[node name=\"A\"]\n\n[component type=\"T\" node=\".\"]\n"
      "m = ExtResource(\"9\")\n",
      &error));
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
  EXPECT_EQ(game->components[0].properties.Get("wall")->Get<mtext::NodePath>()->path,
            "Edges/Left");
  EXPECT_EQ(game->components[0].properties.Get("group")->Get<mtext::NodePath>()->path,
            "Edges");

  ASSERT_TRUE(map.ReparentNode(left, map.GetRoot()));
  EXPECT_EQ(left->GetPath(), "Left");
  EXPECT_EQ(game->components[0].properties.Get("wall")->Get<mtext::NodePath>()->path,
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
