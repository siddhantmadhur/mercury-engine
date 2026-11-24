// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the MCP scene-read tools (editor/mcp/tools/scene_read.cc) on a copy
// of examples/roll_a_ball: get_editor_state, get_component_schema, find_nodes,
// get_node and read_console, plus the Console's sequence numbers they use.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "console.h"
#include "editor_scene.h"
#include "mcp_tools_fixture.h"
#include "mcp/tools/scene_json.h"

using namespace Mercury;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace {

// roll_a_ball: RollABall (root) > Camera, Sun, Ground, Walls (North, South,
// East, West), Player, Pickups (Pickup1..Pickup8): 19 nodes.
constexpr size_t kRollABallNodes = 19;

std::vector<std::string> Names(const json& nodes) {
  std::vector<std::string> names;
  for (const json& node : nodes) names.push_back(node.at("name"));
  return names;
}

uint64_t UidOf(McpToolFixture& fixture, const char* path) {
  const MapNode* node = fixture.Scene().GetMap().FindNode(path);
  EXPECT_NE(node, nullptr) << path;
  return node ? node->uid : 0;
}

// ---------------------------------------------------------- get_editor_state
// --

TEST(McpToolsSceneReadTest, EditorStateDescribesTheOpenProject) {
  McpToolFixture fixture;
  const ToolResult r = fixture.CallTool("get_editor_state");
  ASSERT_TRUE(r.ok) << r.result.dump();
  const json& state = r.result;

  EXPECT_EQ(state["project"]["name"], "Roll a Ball");
  // The project reports its canonical root (macOS temp dirs are symlinks).
  EXPECT_EQ(state["project"]["root"],
            std::filesystem::canonical(fixture.Root()).generic_string());
  EXPECT_EQ(state["project"]["main_map"], "res://maps/main.mmap");
  EXPECT_EQ(state["open_map"]["path"], "res://maps/main.mmap");
  EXPECT_EQ(state["open_map"]["dirty"], false);
  EXPECT_TRUE(state["selection"]["node"].is_null());
  EXPECT_TRUE(state["selection"]["asset"].is_null());
  EXPECT_EQ(state["play_session_running"], false);
  EXPECT_EQ(state["mcp"]["read_only"], false);
}

TEST(McpToolsSceneReadTest, EditorStateReportsReadOnlyFromTheSettings) {
  McpToolFixture fixture;
  const ToolResult r = fixture.CallTool("get_editor_state", json::object(),
                                        /*allow_edits=*/false);
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["mcp"]["read_only"], true);
}

TEST(McpToolsSceneReadTest, EditorStateReportsDirtyAndSelection) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  MapNode* player = scene.GetMap().FindNode("Player");
  ASSERT_NE(player, nullptr);
  scene.Select(player);
  scene.MarkChanged();

  json state = fixture.CallTool("get_editor_state").result;
  EXPECT_EQ(state["open_map"]["dirty"], true);
  const json& node = state["selection"]["node"];
  EXPECT_EQ(node["name"], "Player");
  EXPECT_EQ(node["uid"], player->uid);
  EXPECT_TRUE(node.contains("transform"));
  EXPECT_FALSE(node.contains("components"))
      << "the selection summary leaves components to get_node";
  EXPECT_TRUE(state["selection"]["asset"].is_null());

  scene.SelectAsset("res://assets/player.masset");
  state = fixture.CallTool("get_editor_state").result;
  EXPECT_TRUE(state["selection"]["node"].is_null());
  EXPECT_EQ(state["selection"]["asset"], "res://assets/player.masset");
}

TEST(McpToolsSceneReadTest, EditorStateCameraRecoversYawPitchAndDistance) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();

  // Leaving the 2D view resets the 3D camera to yaw 35, pitch -25.
  scene.SetView2D(true);
  scene.SetView2D(false);
  json camera = fixture.CallTool("get_editor_state").result["camera"];
  EXPECT_EQ(camera["view_2d"], false);
  EXPECT_NEAR(camera["yaw"].get<double>(), 35.0, 0.01);
  EXPECT_NEAR(camera["pitch"].get<double>(), -25.0, 0.01);
  const glm::vec3 target = scene.GetCameraTarget();
  const glm::vec3 position = scene.GetCameraPosition();
  EXPECT_NEAR(camera["distance"].get<double>(), glm::length(target - position),
              0.01);
  ASSERT_EQ(camera["target"].size(), 3u);
  EXPECT_NEAR(camera["target"][0].get<double>(), target.x, 1e-4);
  EXPECT_NEAR(camera["position"][1].get<double>(), position.y, 1e-3);
  // The camera sits above its target when it looks down (negative pitch).
  EXPECT_GT(position.y, target.y);

  scene.SetView2D(true);
  camera = fixture.CallTool("get_editor_state").result["camera"];
  EXPECT_EQ(camera["view_2d"], true);
  EXPECT_EQ(camera["yaw"], 0.0);
  EXPECT_EQ(camera["pitch"], 0.0);
}

TEST(McpToolsSceneReadTest, EditorStateConsoleCountsAndCursor) {
  McpToolFixture fixture;
  Console& console = fixture.Scene().ConsoleRef();
  const json before = fixture.CallTool("get_editor_state").result["console"];
  console.Error("e1");
  console.Error("e2");
  console.Warn("w1");
  console.Info("i1");
  const json after = fixture.CallTool("get_editor_state").result["console"];
  EXPECT_EQ(after["errors"].get<int>(), before["errors"].get<int>() + 2);
  EXPECT_EQ(after["warnings"].get<int>(), before["warnings"].get<int>() + 1);
  EXPECT_EQ(after["latest_cursor"].get<uint64_t>(),
            before["latest_cursor"].get<uint64_t>() + 4);
  EXPECT_EQ(after["latest_cursor"].get<uint64_t>(), console.LatestCursor());
}

// ------------------------------------------------------ get_component_schema
// --

TEST(McpToolsSceneReadTest, ComponentSchemaListsEverything) {
  McpToolFixture fixture;
  const ToolResult r = fixture.CallTool("get_component_schema");
  ASSERT_TRUE(r.ok) << r.result.dump();
  const json& components = r.result["components"];
  const auto rigidbody =
      std::find_if(components.begin(), components.end(),
                   [](const json& c) { return c["name"] == "Rigidbody"; });
  ASSERT_NE(rigidbody, components.end());
  EXPECT_TRUE(rigidbody->contains("category"));
  EXPECT_EQ((*rigidbody)["is_script"], false);
  EXPECT_FALSE(rigidbody->contains("fields"))
      << "the listing is names only; ask for one type for its fields";

  const json& asset_types = r.result["asset_types"];
  const auto material =
      std::find_if(asset_types.begin(), asset_types.end(),
                   [](const json& a) { return a["name"] == "Material"; });
  ASSERT_NE(material, asset_types.end());
}

TEST(McpToolsSceneReadTest, ComponentSchemaForOneComponent) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("get_component_schema", {{"type", "Rigidbody"}});
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["kind"], "component");
  EXPECT_EQ(r.result["name"], "Rigidbody");
  const json& fields = r.result["fields"];
  const auto motion =
      std::find_if(fields.begin(), fields.end(),
                   [](const json& f) { return f["name"] == "motion_type"; });
  ASSERT_NE(motion, fields.end());
  EXPECT_EQ((*motion)["type"], "enum");
  EXPECT_EQ((*motion)["enum_values"],
            (std::vector<std::string>{"static", "kinematic", "dynamic"}));
}

TEST(McpToolsSceneReadTest, ComponentSchemaForAnAssetType) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("get_component_schema", {{"type", "Material"}});
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["kind"], "asset_type");
  EXPECT_TRUE(r.result["fields"].is_array());
}

TEST(McpToolsSceneReadTest, ComponentSchemaSuggestsForATypo) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("get_component_schema", {{"type", "Rigidbdy"}});
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.result["suggestion"], "Rigidbody") << r.result.dump();
  EXPECT_NE(r.result["error"].get<std::string>().find("Rigidbdy"),
            std::string::npos);

  const ToolResult nothing =
      fixture.CallTool("get_component_schema", {{"type", "zzzzzzzzzzzz"}});
  ASSERT_FALSE(nothing.ok);
  EXPECT_FALSE(nothing.result.contains("suggestion"));

  EXPECT_FALSE(fixture.CallTool("get_component_schema", {{"type", 3}}).ok);
}

// ----------------------------------------------------------------- find_nodes
// --

TEST(McpToolsSceneReadTest, FindNodesWithoutFiltersListsTheMapInOrder) {
  McpToolFixture fixture;
  const ToolResult r = fixture.CallTool("find_nodes");
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["truncated"], false);
  const std::vector<std::string> names = Names(r.result["nodes"]);
  ASSERT_EQ(names.size(), kRollABallNodes);
  EXPECT_EQ(names.front(), "RollABall");
  EXPECT_EQ(names[1], "Camera");
  // Parents come before their children.
  const auto at = [&](const char* name) {
    return std::find(names.begin(), names.end(), name) - names.begin();
  };
  EXPECT_LT(at("Walls"), at("North"));
  EXPECT_FALSE(r.result["nodes"][0].contains("components"));
  EXPECT_EQ(r.result["nodes"][0]["path"], "RollABall");
}

TEST(McpToolsSceneReadTest, FindNodesByNameGlob) {
  McpToolFixture fixture;
  // "?" is exactly one character: Pickup1..8 and also "Pickups".
  json r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup?"}}).result;
  EXPECT_EQ(r["nodes"].size(), 9u);
  r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup[0-9]"}}).result;
  EXPECT_EQ(r["nodes"].size(), 0u) << "only * and ? are wildcards";
  r = fixture.CallTool("find_nodes", {{"name_glob", "pick*"}}).result;
  EXPECT_EQ(r["nodes"].size(), 9u) << "case-insensitive: Pickups + 8 pickups";
  r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup?*"}}).result;
  EXPECT_EQ(r["nodes"].size(), 9u);
  r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup8"}}).result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"Pickup8"}));
  r = fixture.CallTool("find_nodes", {{"name_glob", "*s"}}).result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"Walls", "Pickups"}));
  r = fixture.CallTool("find_nodes", {{"name_glob", "Player"}}).result;
  ASSERT_EQ(r["nodes"].size(), 1u);
  r = fixture.CallTool("find_nodes", {{"name_glob", "Nope*"}}).result;
  EXPECT_EQ(r["nodes"].size(), 0u);
  EXPECT_EQ(r["truncated"], false);
}

TEST(McpToolsSceneReadTest, FindNodesByComponent) {
  McpToolFixture fixture;
  json r = fixture.CallTool("find_nodes", {{"component", "Rigidbody"}}).result;
  EXPECT_EQ(r["nodes"].size(), 9u) << "the player and the 8 pickups";
  r = fixture.CallTool("find_nodes", {{"component", "PlayerController"}})
          .result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"Player"}));
  r = fixture.CallTool("find_nodes", {{"component", "BoxCollider"}}).result;
  EXPECT_GE(r["nodes"].size(), 5u);
  for (const json& node : r["nodes"]) {
    const MapNode* map_node =
        fixture.Scene().GetMap().FindByUid(node["uid"].get<uint64_t>());
    ASSERT_NE(map_node, nullptr);
    EXPECT_NE(map_node->FindComponent("BoxCollider"), nullptr);
  }
  // A real component that nothing in the map uses is just an empty result.
  r = fixture.CallTool("find_nodes", {{"component", "Animator"}}).result;
  EXPECT_EQ(r["nodes"].size(), 0u);
}

TEST(McpToolsSceneReadTest, FindNodesUnknownComponentSuggests) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("find_nodes", {{"component", "BoxColider"}});
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.result["suggestion"], "BoxCollider") << r.result.dump();
}

TEST(McpToolsSceneReadTest, FindNodesByTag) {
  McpToolFixture fixture;
  // A node's tag is the string MapNode::tag.
  for (const char* name : {"Pickups/Pickup1", "Pickups/Pickup2"}) {
    fixture.Scene().GetMap().FindNode(name)->tag = "Collectible";
  }
  fixture.Scene().GetMap().FindNode("Player")->tag = "Player";

  json r = fixture.CallTool("find_nodes", {{"tag", "Collectible"}}).result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"Pickup1", "Pickup2"}))
      << "a tag the project does not define is fine when a node carries it";
  r = fixture.CallTool("find_nodes", {{"tag", "Player"}}).result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"Player"}));
  r = fixture.CallTool("find_nodes", {{"tag", "collectible"}}).result;
  EXPECT_TRUE(r.contains("error")) << "tags are case-sensitive";

  // A tag nobody has and the project does not define is an error.
  const ToolResult typo = fixture.CallTool("find_nodes", {{"tag", "Playr"}});
  ASSERT_FALSE(typo.ok);
  EXPECT_EQ(typo.result["suggestion"], "Player") << typo.result.dump();
  // A built-in tag that no node has is an empty result.
  r = fixture.CallTool("find_nodes", {{"tag", "Respawn"}}).result;
  EXPECT_EQ(r["nodes"].size(), 0u);
  EXPECT_FALSE(r.contains("error"));
}

TEST(McpToolsSceneReadTest, FindNodesByPathPrefix) {
  McpToolFixture fixture;
  json r = fixture.CallTool("find_nodes", {{"path_prefix", "RollABall/Walls/"}})
               .result;
  EXPECT_EQ(Names(r["nodes"]),
            (std::vector<std::string>{"North", "South", "East", "West"}));
  r = fixture.CallTool("find_nodes", {{"path_prefix", "RollABall/Walls"}})
          .result;
  EXPECT_EQ(r["nodes"].size(), 5u) << "a plain prefix includes Walls itself";
}

TEST(McpToolsSceneReadTest, FindNodesCombinesFilters) {
  McpToolFixture fixture;
  const json r =
      fixture
          .CallTool("find_nodes", {{"name_glob", "*o*"},
                                   {"component", "BoxCollider"},
                                   {"path_prefix", "RollABall/Walls"}})
          .result;
  EXPECT_EQ(Names(r["nodes"]), (std::vector<std::string>{"North", "South"}));
}

TEST(McpToolsSceneReadTest, FindNodesLimitTruncates) {
  McpToolFixture fixture;
  json r = fixture.CallTool("find_nodes", {{"limit", 3}}).result;
  EXPECT_EQ(r["nodes"].size(), 3u);
  EXPECT_EQ(r["truncated"], true);
  r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup?"}, {"limit", 8}})
          .result;
  EXPECT_EQ(r["nodes"].size(), 8u);
  EXPECT_EQ(r["truncated"], true) << "9 nodes match";
  r = fixture.CallTool("find_nodes", {{"name_glob", "Pickup?"}, {"limit", 9}})
          .result;
  EXPECT_EQ(r["nodes"].size(), 9u);
  EXPECT_EQ(r["truncated"], false) << "exactly the limit is not truncated";
  r = fixture.CallTool("find_nodes", {{"limit", kRollABallNodes}}).result;
  EXPECT_EQ(r["truncated"], false);
  // Above the maximum the limit is clamped, not rejected.
  r = fixture.CallTool("find_nodes", {{"limit", 100000}}).result;
  EXPECT_EQ(r["nodes"].size(), kRollABallNodes);
  EXPECT_EQ(r["truncated"], false);
}

TEST(McpToolsSceneReadTest, FindNodesRejectsBadArguments) {
  McpToolFixture fixture;
  EXPECT_FALSE(fixture.CallTool("find_nodes", {{"limit", 0}}).ok);
  EXPECT_FALSE(fixture.CallTool("find_nodes", {{"limit", "ten"}}).ok);
  EXPECT_FALSE(fixture.CallTool("find_nodes", {{"name_glob", 5}}).ok);
  EXPECT_FALSE(fixture.CallTool("find_nodes", {{"tag", json::array()}}).ok);
}

// -------------------------------------------------------------------- get_node
// --

TEST(McpToolsSceneReadTest, GetNodeByPathWithComponents) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("get_node", {{"node", {{"path", "RollABall/Player"}}}});
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["name"], "Player");
  EXPECT_EQ(r.result["path"], "RollABall/Player");
  EXPECT_EQ(r.result["node_path"], "Player");
  EXPECT_EQ(r.result["uid"], UidOf(fixture, "Player"));
  ASSERT_TRUE(r.result.contains("components"));
  std::vector<std::string> types;
  for (const json& component : r.result["components"]) {
    types.push_back(component["type"]);
    EXPECT_TRUE(component["props"].is_object());
  }
  EXPECT_NE(std::find(types.begin(), types.end(), "Rigidbody"), types.end());
  EXPECT_NE(std::find(types.begin(), types.end(), "Transform"), types.end());
  EXPECT_FALSE(r.result.contains("children"));
}

TEST(McpToolsSceneReadTest, GetNodeByUidNumberAndString) {
  McpToolFixture fixture;
  const uint64_t uid = UidOf(fixture, "Walls/North");
  const ToolResult by_number =
      fixture.CallTool("get_node", {{"node", {{"uid", uid}}}});
  ASSERT_TRUE(by_number.ok) << by_number.result.dump();
  EXPECT_EQ(by_number.result["name"], "North");
  EXPECT_EQ(by_number.result["path"], "RollABall/Walls/North");
  const ToolResult by_string =
      fixture.CallTool("get_node", {{"node", {{"uid", std::to_string(uid)}}}});
  ASSERT_TRUE(by_string.ok) << by_string.result.dump();
  EXPECT_EQ(by_string.result, by_number.result);
}

TEST(McpToolsSceneReadTest, GetNodeWithoutComponents) {
  McpToolFixture fixture;
  const ToolResult r = fixture.CallTool(
      "get_node",
      {{"node", {{"path", "Player"}}}, {"include_components", false}});
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_FALSE(r.result.contains("components"));
  EXPECT_TRUE(r.result.contains("transform"));
  EXPECT_EQ(r.result["parent_uid"], UidOf(fixture, "."));
}

TEST(McpToolsSceneReadTest, GetNodeWithChildren) {
  McpToolFixture fixture;
  const ToolResult r =
      fixture.CallTool("get_node", {{"node", {{"path", "RollABall/Walls"}}},
                                    {"include_children", true},
                                    {"include_components", false}});
  ASSERT_TRUE(r.ok) << r.result.dump();
  EXPECT_EQ(r.result["child_count"], 4);
  const json& children = r.result["children"];
  ASSERT_EQ(children.size(), 4u);
  EXPECT_EQ(Names(children),
            (std::vector<std::string>{"North", "South", "East", "West"}));
  EXPECT_EQ(children[0]["uid"], UidOf(fixture, "Walls/North"));
  EXPECT_EQ(children[0].size(), 2u) << "children are just {name, uid}";

  // A leaf has an empty list, not a missing one.
  const ToolResult leaf = fixture.CallTool(
      "get_node", {{"node", {{"path", "Player"}}}, {"include_children", true}});
  ASSERT_TRUE(leaf.ok);
  EXPECT_TRUE(leaf.result["children"].empty());
  EXPECT_TRUE(leaf.result.contains("components"));
}

TEST(McpToolsSceneReadTest, GetNodeMatchesTheSceneJsonHelper) {
  McpToolFixture fixture;
  const MapNode* player = fixture.Scene().GetMap().FindNode("Player");
  ASSERT_NE(player, nullptr);
  const ToolResult r =
      fixture.CallTool("get_node", {{"node", {{"uid", player->uid}}}});
  EXPECT_EQ(r.result, mcp::NodeToJson(fixture.Scene(), *player, true));
}

TEST(McpToolsSceneReadTest, GetNodeErrorsSuggest) {
  McpToolFixture fixture;
  ToolResult r =
      fixture.CallTool("get_node", {{"node", {{"path", "RollABall/Plyer"}}}});
  ASSERT_FALSE(r.ok);
  EXPECT_EQ(r.result["suggestion"], "RollABall/Player") << r.result.dump();

  r = fixture.CallTool("get_node", {{"node", {{"uid", 987654321}}}});
  ASSERT_FALSE(r.ok);
  EXPECT_TRUE(r.result.contains("error"));

  r = fixture.CallTool("get_node", json::object());
  ASSERT_FALSE(r.ok);
  r = fixture.CallTool("get_node",
                       {{"node", {{"uid", 1}, {"path", "Player"}}}});
  ASSERT_FALSE(r.ok) << "exactly one of uid and path";
  r = fixture.CallTool("get_node", {{"node", {{"path", "Player"}}},
                                    {"include_children", "yes"}});
  ASSERT_FALSE(r.ok);
}

// ---------------------------------------------------------------- read_console
// --

// The console of a fixture may already hold lines from opening the project:
// every test reads relative to a cursor taken after those.
struct ConsoleCase {
  McpToolFixture fixture;
  Console& console = fixture.Scene().ConsoleRef();
  uint64_t start = console.LatestCursor();

  json Read(json args) { return fixture.CallTool("read_console", args).result; }
};

std::vector<std::string> Texts(const json& result) {
  std::vector<std::string> texts;
  for (const json& entry : result["entries"]) texts.push_back(entry["text"]);
  return texts;
}

TEST(McpToolsSceneReadTest, ReadConsoleFromACursorReadsEachEntryOnce) {
  ConsoleCase c;
  c.console.Info("one");
  c.console.Warn("two");
  c.console.Error("three");

  json r = c.Read({{"cursor", c.start}});
  ASSERT_EQ(r["entries"].size(), 3u) << r.dump();
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"one", "two", "three"}));
  EXPECT_EQ(r["entries"][0]["seq"], c.start);
  EXPECT_EQ(r["entries"][2]["seq"], c.start + 2);
  EXPECT_EQ(r["entries"][0]["level"], "info");
  EXPECT_EQ(r["entries"][1]["level"], "warning");
  EXPECT_EQ(r["entries"][2]["level"], "error");
  EXPECT_EQ(r["entries"][0]["source"], "editor");
  EXPECT_EQ(r["next_cursor"], c.start + 3);
  EXPECT_EQ(r["truncated"], false);
  EXPECT_EQ(r["dropped"], false);

  // Nothing new: an empty page that keeps the cursor.
  json again = c.Read({{"cursor", r["next_cursor"]}});
  EXPECT_TRUE(again["entries"].empty());
  EXPECT_EQ(again["next_cursor"], r["next_cursor"]);

  c.console.Info("four");
  again = c.Read({{"cursor", r["next_cursor"]}});
  EXPECT_EQ(Texts(again), (std::vector<std::string>{"four"}));
  EXPECT_EQ(again["next_cursor"], c.start + 4);
}

TEST(McpToolsSceneReadTest, ReadConsoleSourcesAreNamed) {
  ConsoleCase c;
  c.console.Add(Console::Level::kInfo, Console::Source::kBuild, "b");
  c.console.Add(Console::Level::kInfo, Console::Source::kGame, "g");
  c.console.Add(Console::Level::kInfo, Console::Source::kLore, "l");
  const json r = c.Read({{"cursor", c.start}});
  ASSERT_EQ(r["entries"].size(), 3u);
  EXPECT_EQ(r["entries"][0]["source"], "build");
  EXPECT_EQ(r["entries"][1]["source"], "game");
  EXPECT_EQ(r["entries"][2]["source"], "lore");
}

TEST(McpToolsSceneReadTest, ReadConsoleWithoutACursorIsTheTail) {
  ConsoleCase c;
  for (int i = 0; i < 10; i++) c.console.Info("line " + std::to_string(i));
  json r = c.Read({{"limit", 3}});
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"line 7", "line 8", "line 9"}));
  EXPECT_EQ(r["truncated"], true) << "older entries exist";
  EXPECT_EQ(r["next_cursor"], c.console.LatestCursor());
  EXPECT_EQ(r["dropped"], false);

  r = c.Read({{"limit", 1000}});
  EXPECT_EQ(r["truncated"], false);
  EXPECT_EQ(r["entries"].back()["text"], "line 9");
}

TEST(McpToolsSceneReadTest, ReadConsoleLimitPagesForward) {
  ConsoleCase c;
  for (int i = 0; i < 7; i++) c.console.Info("n" + std::to_string(i));

  json page = c.Read({{"cursor", c.start}, {"limit", 3}});
  EXPECT_EQ(Texts(page), (std::vector<std::string>{"n0", "n1", "n2"}));
  EXPECT_EQ(page["truncated"], true);
  EXPECT_EQ(page["next_cursor"], c.start + 3);

  page = c.Read({{"cursor", page["next_cursor"]}, {"limit", 3}});
  EXPECT_EQ(Texts(page), (std::vector<std::string>{"n3", "n4", "n5"}));
  EXPECT_EQ(page["truncated"], true);

  page = c.Read({{"cursor", page["next_cursor"]}, {"limit", 3}});
  EXPECT_EQ(Texts(page), (std::vector<std::string>{"n6"}));
  EXPECT_EQ(page["truncated"], false);
  EXPECT_EQ(page["next_cursor"], c.start + 7);
}

TEST(McpToolsSceneReadTest, ReadConsoleMinLevelFilters) {
  ConsoleCase c;
  c.console.Info("i1");
  c.console.Warn("w1");
  c.console.Error("e1");
  c.console.Info("i2");
  c.console.Warn("w2");

  json r = c.Read({{"cursor", c.start}, {"min_level", "info"}});
  EXPECT_EQ(r["entries"].size(), 5u);
  r = c.Read({{"cursor", c.start}, {"min_level", "warning"}});
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"w1", "e1", "w2"}));
  EXPECT_EQ(r["next_cursor"], c.start + 5)
      << "the cursor moves past the filtered-out lines too";
  r = c.Read({{"cursor", c.start}, {"min_level", "error"}});
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"e1"}));
  // seq stays the line's own number, not its place in the filtered list.
  EXPECT_EQ(r["entries"][0]["seq"], c.start + 2);

  // The limit counts matching entries only; the cursor stops after the last
  // one returned so the skipped matches are not lost.
  r = c.Read({{"cursor", c.start}, {"min_level", "warning"}, {"limit", 2}});
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"w1", "e1"}));
  EXPECT_EQ(r["truncated"], true);
  r = c.Read({{"cursor", r["next_cursor"]}, {"min_level", "warning"}});
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"w2"}));

  // The tail honours the filter too.
  r = c.Read({{"min_level", "error"}, {"limit", 5}});
  EXPECT_EQ(r["entries"].back()["text"], "e1");
  for (const json& entry : r["entries"]) EXPECT_EQ(entry["level"], "error");
}

TEST(McpToolsSceneReadTest, ReadConsoleEvictionSetsDropped) {
  ConsoleCase c;
  c.console.Info("first");
  const uint64_t first_seq = c.start;
  const json early = c.Read({{"cursor", first_seq}});
  ASSERT_EQ(early["dropped"], false);

  // Far more than the 5000 lines the console keeps.
  for (int i = 0; i < 6000; i++) c.console.Info("flood " + std::to_string(i));
  const uint64_t latest = c.console.LatestCursor();
  EXPECT_EQ(latest, first_seq + 6001);

  // The reader's cursor now points at lines that are gone.
  json r = c.Read({{"cursor", first_seq}, {"limit", 1000}});
  EXPECT_EQ(r["dropped"], true);
  ASSERT_FALSE(r["entries"].empty());
  const uint64_t oldest = r["entries"][0]["seq"];
  EXPECT_GT(oldest, first_seq);
  EXPECT_EQ(oldest, latest - 5000) << "the console holds exactly 5000 lines";
  EXPECT_EQ(r["entries"][0]["text"],
            "flood " + std::to_string(oldest - first_seq - 1));
  EXPECT_EQ(r["truncated"], true);

  // Cursors that are still held are fine, and seq did not restart.
  r = c.Read({{"cursor", latest - 2}});
  EXPECT_EQ(r["dropped"], false);
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"flood 5998", "flood 5999"}));
  EXPECT_EQ(r["entries"][0]["seq"], latest - 2);

  // Exactly at the oldest held line is not a drop.
  r = c.Read({{"cursor", oldest}, {"limit", 1}});
  EXPECT_EQ(r["dropped"], false);
  // One before it is.
  r = c.Read({{"cursor", oldest - 1}, {"limit", 1}});
  EXPECT_EQ(r["dropped"], true);
  EXPECT_EQ(r["entries"][0]["seq"], oldest);

  // Paging from a dropped cursor through next_cursor reaches the end.
  uint64_t cursor = first_seq;
  size_t seen = 0;
  while (true) {
    r = c.Read({{"cursor", cursor}, {"limit", 1000}});
    seen += r["entries"].size();
    cursor = r["next_cursor"];
    if (!r["truncated"].get<bool>()) break;
  }
  EXPECT_EQ(cursor, latest);
  EXPECT_EQ(seen, 5000u);
  EXPECT_EQ(c.Read({{"cursor", cursor}})["dropped"], false);
}

TEST(McpToolsSceneReadTest, ReadConsoleClearDropsButKeepsSequenceNumbers) {
  ConsoleCase c;
  c.console.Info("before");
  const uint64_t cursor = c.start;
  c.console.Clear();
  c.console.Info("after");

  const json r = c.Read({{"cursor", cursor}});
  EXPECT_EQ(r["dropped"], true);
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"after"}));
  EXPECT_EQ(r["entries"][0]["seq"], cursor + 1) << "seq survives Clear()";
  EXPECT_EQ(r["next_cursor"], cursor + 2);
}

TEST(McpToolsSceneReadTest, ReadConsoleRejectsBadArguments) {
  ConsoleCase c;
  json r = c.fixture.CallTool("read_console", {{"min_level", "warnng"}}).result;
  EXPECT_EQ(r["suggestion"], "warning") << r.dump();
  EXPECT_FALSE(c.fixture.CallTool("read_console", {{"min_level", 3}}).ok);
  EXPECT_FALSE(c.fixture.CallTool("read_console", {{"cursor", -1}}).ok);
  EXPECT_FALSE(c.fixture.CallTool("read_console", {{"cursor", "7"}}).ok);
  EXPECT_FALSE(c.fixture.CallTool("read_console", {{"limit", 0}}).ok);

  const ToolResult ahead = c.fixture.CallTool(
      "read_console", {{"cursor", c.console.LatestCursor() + 100}});
  ASSERT_FALSE(ahead.ok);
  EXPECT_NE(ahead.result["error"].get<std::string>().find("ahead"),
            std::string::npos);
  // The latest cursor itself is valid: it reads nothing yet.
  const ToolResult latest = c.fixture.CallTool(
      "read_console", {{"cursor", c.console.LatestCursor()}});
  ASSERT_TRUE(latest.ok);
  EXPECT_TRUE(latest.result["entries"].empty());
}

TEST(McpToolsSceneReadTest, EditorStateCursorFeedsReadConsole) {
  McpToolFixture fixture;
  Console& console = fixture.Scene().ConsoleRef();
  const uint64_t cursor =
      fixture.CallTool("get_editor_state").result["console"]["latest_cursor"];
  console.Warn("after the snapshot");
  const json r = fixture.CallTool("read_console", {{"cursor", cursor}}).result;
  EXPECT_EQ(Texts(r), (std::vector<std::string>{"after the snapshot"}));
  EXPECT_EQ(r["dropped"], false);
}

// All five tools only look: none is rejected by a read-only server.
TEST(McpToolsSceneReadTest, AllToolsWorkOnAReadOnlyServer) {
  McpToolFixture fixture;
  for (const char* name : {"get_editor_state", "get_component_schema",
                           "find_nodes", "read_console"}) {
    EXPECT_TRUE(fixture.CallTool(name, json::object(), false).ok) << name;
  }
  EXPECT_TRUE(
      fixture.CallTool("get_node", {{"node", {{"path", "Player"}}}}, false).ok);
}

}  // namespace
