// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the MCP scene write tools (editor/mcp/tools/scene_write.cc):
// apply_scene_operations (atomic batches, one undo step, aliases, every field
// type, prefabs, errors with suggestions), select_node, save_map, undo, redo.
// Everything runs through the tool registry on a copy of examples/roll_a_ball.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "mcp/tools/scene_json.h"
#include "mcp_tools_fixture.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

namespace fs = std::filesystem;
using namespace Mercury;
using Mercury::mcp::NodeToJson;
using Mercury::mcp::ResolveNode;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace {

// The whole map as text: the byte-identical check of "nothing was applied".
std::string MapText(McpToolFixture& fixture) {
  return fixture.Scene().GetMap().SaveToString();
}

ToolResult Apply(McpToolFixture& fixture, json operations,
                 json select = nullptr) {
  json args{{"operations", std::move(operations)}};
  if (!select.is_null()) args["select"] = std::move(select);
  return fixture.CallTool("apply_scene_operations", args);
}

// The node's JSON with components, by path; null when there is no such node.
json NodeAt(McpToolFixture& fixture, const std::string& path) {
  std::string error;
  MapNode* node =
      ResolveNode(fixture.Scene(), json{{"path", path}}, &error);
  if (!node) return nullptr;
  return NodeToJson(fixture.Scene(), *node, true);
}

// The stored props of one component of a node ({} when it has none).
json PropsOf(const json& node, const std::string& type) {
  if (!node.is_object()) return nullptr;
  for (const json& component : node["components"]) {
    if (component["type"] == type) return component["props"];
  }
  return nullptr;
}

bool HasComponent(const json& node, const std::string& type) {
  return !PropsOf(node, type).is_null();
}

std::string ErrorOf(const ToolResult& result) {
  return result.result.value("error", std::string());
}

bool Near(const json& array, std::initializer_list<float> expected,
          float epsilon = 1e-3f) {
  if (!array.is_array() || array.size() != expected.size()) return false;
  size_t i = 0;
  for (float value : expected) {
    if (std::abs(array[i].get<float>() - value) > epsilon) return false;
    i++;
  }
  return true;
}

// A world transform from the nodes' JSON, composed the way the runtime does.
struct World {
  glm::vec3 position{0.0f};
  glm::quat rotation{1, 0, 0, 0};
  glm::vec3 scale{1.0f};
};

World WorldOf(McpToolFixture& fixture, const std::string& full_path) {
  // Walk from the root: "A/B/C" -> A, A/B, A/B/C.
  World world;
  std::string prefix;
  size_t start = 0;
  while (start <= full_path.size()) {
    const size_t slash = full_path.find('/', start);
    const size_t end = slash == std::string::npos ? full_path.size() : slash;
    prefix = full_path.substr(0, end);
    const json node = NodeAt(fixture, prefix);
    EXPECT_FALSE(node.is_null()) << prefix;
    if (node.is_null()) return world;
    const json& t = node["transform"];
    const glm::vec3 position(t["position"][0].get<float>(),
                             t["position"][1].get<float>(),
                             t["position"][2].get<float>());
    const glm::quat rotation(t["rotation"][3].get<float>(),
                             t["rotation"][0].get<float>(),
                             t["rotation"][1].get<float>(),
                             t["rotation"][2].get<float>());
    const glm::vec3 scale(t["scale"][0].get<float>(), t["scale"][1].get<float>(),
                          t["scale"][2].get<float>());
    world.position = world.position + world.rotation * (world.scale * position);
    world.rotation = world.rotation * rotation;
    world.scale = world.scale * scale;
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return world;
}

// A component with one field of every FieldType, its defaults unlike the test
// values, registered as a schema script (no attach function), which the next
// project open or ClearSchemaScripts removes again.
FieldInfo MakeField(const char* name, FieldType type, mtext::Variant value) {
  FieldInfo field;
  field.name = name;
  field.type = type;
  field.default_value = std::move(value);
  return field;
}

struct FieldCase {
  FieldInfo field;
  json value;  // what a client sends, and what the read tools return
};

std::vector<FieldCase> EveryFieldCase() {
  std::vector<FieldCase> cases;
  cases.push_back({MakeField("flag", FieldType::kBool, false), true});
  cases.push_back({MakeField("count", FieldType::kInt, int64_t{0}), -7});
  cases.push_back({MakeField("speed", FieldType::kFloat, 1.0), 2.5});
  cases.push_back(
      {MakeField("label", FieldType::kString, std::string("")), "hi"});
  cases.push_back({MakeField("size", FieldType::kVector2, glm::vec2(0.0f)),
                   json::array({1.5, -2.0})});
  cases.push_back({MakeField("offset", FieldType::kVector3, glm::vec3(0.0f)),
                   json::array({0.25, 8.0, -1.0})});
  cases.push_back(
      {MakeField("tint", FieldType::kColor,
                 mtext::Color{glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)}),
       json::array({1.0, 0.5, 0.25, 0.75})});
  cases.push_back({MakeField("target", FieldType::kNodeRef, mtext::NodePath{""}),
                   json{{"path", "Walls/North"}}});
  FieldInfo asset = MakeField("material", FieldType::kAsset, mtext::Resource{""});
  asset.asset_type = "Material";
  cases.push_back({asset, json{{"res", "res://assets/wall.masset"}}});
  FieldInfo mode = MakeField("mode", FieldType::kEnum, std::string("slow"));
  mode.enum_values = {"slow", "fast", "ludicrous"};
  cases.push_back({mode, "fast"});
  FieldInfo ints = MakeField("ids", FieldType::kList, mtext::Array{});
  ints.element_type = FieldType::kInt;
  cases.push_back({ints, json::array({1, 2})});
  FieldInfo names = MakeField("names", FieldType::kList, mtext::Array{});
  names.element_type = FieldType::kString;
  cases.push_back({names, json::array({"a", "b"})});
  FieldInfo assets = MakeField("materials", FieldType::kList, mtext::Array{});
  assets.element_type = FieldType::kAsset;
  cases.push_back(
      {assets, json::array({json{{"res", "res://assets/wall.masset"}}})});
  cases.push_back({MakeField("on_hit", FieldType::kEvent, mtext::Array{}),
                   json::array({json::array({json{{"path", "Walls"}}, "Door",
                                             "Open", true})})});
  return cases;
}

void RegisterEveryFieldComponent() {
  ComponentInfo info;
  info.name = "McpEveryField";
  info.category = "Scripts";
  info.is_script = true;  // no attach function: a schema script
  for (const FieldCase& entry : EveryFieldCase()) {
    info.fields.push_back(entry.field);
  }
  TypeRegistry::Instance().RegisterComponent(std::move(info));
}

// A fixture that can write a prefab into its project.
void WritePrefab(McpToolFixture& fixture, const std::string& name,
                 const std::string& text) {
  fs::create_directories(fixture.Root() / "prefabs");
  std::ofstream(fixture.Root() / "prefabs" / name) << text;
}

constexpr const char* kCratePrefab = R"([mprefab]
format = 2

[[node]]
name = "Crate"

[[node]]
name = "Lid"
parent = "."

[[component]]
node = "."
type = "Transform"
[component.props]
position = { vec3 = [1.0, 0.0, 0.0] }

[[component]]
node = "Lid"
type = "Transform"
[component.props]
position = { vec3 = [0.0, 1.0, 0.0] }

[[component]]
node = "Lid"
type = "MeshRenderer"
)";

}  // namespace

// ------------------------------------------------------------------ create --

TEST(McpToolsSceneWriteTest, CreateAddsANodeWithTransformAndComponents) {
  McpToolFixture fixture;
  const ToolResult result = Apply(
      fixture,
      json::array({json{
          {"op", "create"},
          {"name", "Crate"},
          {"parent", json{{"path", "RollABall/Walls"}}},
          {"transform",
           json{{"position", json::array({1, 2, 3})},
                {"rotation_euler_degrees", json::array({0, 90, 0})},
                {"scale", json::array({2, 2, 2})}}},
          {"components",
           json::array({json{{"type", "MeshRenderer"},
                             {"props", json{{"mesh", "sphere"},
                                            {"material",
                                             json{{"res",
                                                   "res://assets/wall.masset"}}}}}},
                        json{{"type", "BoxCollider"}}})},
          {"id", "crate"}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["applied"], 1);
  ASSERT_EQ(result.result["created"].size(), 1u);
  const json& created = result.result["created"][0];
  EXPECT_EQ(created["alias"], "crate");
  EXPECT_EQ(created["path"], "RollABall/Walls/Crate");
  EXPECT_EQ(created["op"], 0);
  EXPECT_TRUE(result.result["warnings"].empty()) << result.result.dump();

  const json node = NodeAt(fixture, "RollABall/Walls/Crate");
  ASSERT_TRUE(node.is_object());
  EXPECT_EQ(node["uid"], created["uid"]);
  EXPECT_TRUE(Near(node["transform"]["position"], {1, 2, 3}));
  EXPECT_TRUE(Near(node["transform"]["euler_degrees"], {0, 90, 0}));
  EXPECT_TRUE(Near(node["transform"]["scale"], {2, 2, 2}));
  const json mesh = PropsOf(node, "MeshRenderer");
  EXPECT_EQ(mesh["mesh"], "sphere");
  EXPECT_EQ(mesh["material"], (json{{"res", "res://assets/wall.masset"}}));
  EXPECT_TRUE(HasComponent(node, "BoxCollider"));
  EXPECT_TRUE(fixture.Scene().IsDirty());
}

TEST(McpToolsSceneWriteTest, CreateDefaultsToTheRootAndRenamesDuplicates) {
  McpToolFixture fixture;
  const ToolResult result = Apply(
      fixture, json::array({json{{"op", "create"}, {"name", "Walls"}},
                            json{{"op", "create"}, {"name", "a/b"}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  // "Walls" exists under the root: the new one is numbered, and a warning says
  // so; the slash in "a/b" is stripped, also with a warning.
  EXPECT_EQ(result.result["created"][0]["path"], "RollABall/Walls2");
  EXPECT_EQ(result.result["created"][1]["path"], "RollABall/ab");
  EXPECT_EQ(result.result["warnings"].size(), 2u) << result.result.dump();
  // A default transform stores no properties at all.
  const json node = NodeAt(fixture, "RollABall/Walls2");
  EXPECT_TRUE(PropsOf(node, "Transform").empty());
}

TEST(McpToolsSceneWriteTest, QuaternionRotationIsStoredAsEulerDegrees) {
  McpToolFixture fixture;
  const float half = std::sqrt(0.5f);
  const ToolResult result = Apply(
      fixture, json::array({json{{"op", "create"},
                                 {"name", "Turned"},
                                 {"transform", json{{"rotation_quat",
                                                     json::array({0, half, 0, half})}}}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  const json node = NodeAt(fixture, "RollABall/Turned");
  EXPECT_TRUE(Near(node["transform"]["euler_degrees"], {0, 90, 0}, 1e-2f))
      << node.dump();
  EXPECT_TRUE(Near(node["transform"]["rotation"], {0, half, 0, half}, 1e-3f));

  // Both forms at once, and an all-zero quaternion, are refused.
  const ToolResult both = Apply(
      fixture,
      json::array({json{{"op", "set_transform"},
                        {"node", json{{"path", "RollABall/Turned"}}},
                        {"rotation_quat", json::array({0, 0, 0, 1})},
                        {"rotation_euler_degrees", json::array({0, 0, 0})}}}));
  EXPECT_FALSE(both.ok);
  EXPECT_NE(ErrorOf(both).find("operations[0]"), std::string::npos);
  const ToolResult zero = Apply(
      fixture,
      json::array({json{{"op", "set_transform"},
                        {"node", json{{"path", "RollABall/Turned"}}},
                        {"rotation_quat", json::array({0, 0, 0, 0})}}}));
  EXPECT_FALSE(zero.ok);
  EXPECT_NE(ErrorOf(zero).find("all zeros"), std::string::npos) << ErrorOf(zero);
}

// ------------------------------------------------------------ undo & redo ---

TEST(McpToolsSceneWriteTest, OneUndoRevertsTheWholeBatch) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  const std::string before = MapText(fixture);
  EXPECT_FALSE(scene.CanUndo());

  const ToolResult result = Apply(
      fixture,
      json::array(
          {json{{"op", "create"}, {"name", "A"}, {"id", "a"}},
           json{{"op", "create"}, {"name", "B"}, {"parent", json{{"alias", "a"}}}},
           json{{"op", "set_transform"},
                {"node", json{{"path", "RollABall/Player"}}},
                {"position", json::array({5, 5, 5})}},
           json{{"op", "delete"}, {"node", json{{"path", "RollABall/Pickups/Pickup1"}}}},
           json{{"op", "add_component"},
                {"node", json{{"path", "RollABall/Walls"}}},
                {"type", "Rigidbody"}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  const std::string after = MapText(fixture);
  EXPECT_NE(before, after);
  EXPECT_TRUE(scene.CanUndo());

  scene.Undo();  // the editor's own undo, as Ctrl+Z
  EXPECT_EQ(MapText(fixture), before) << "one Undo must revert all five ops";
  EXPECT_FALSE(scene.CanUndo()) << "the batch was exactly one undo step";
  EXPECT_TRUE(scene.CanRedo());

  scene.Redo();
  EXPECT_EQ(MapText(fixture), after);
}

TEST(McpToolsSceneWriteTest, UndoAndRedoTools) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);

  // Nothing to undo: not an error, nothing changes.
  ToolResult nothing = fixture.CallTool("undo");
  ASSERT_TRUE(nothing.ok) << nothing.result.dump();
  EXPECT_EQ(nothing.result["undone"], false);
  EXPECT_EQ(nothing.result["can_undo"], false);
  EXPECT_EQ(nothing.result["can_redo"], false);
  nothing = fixture.CallTool("redo");
  ASSERT_TRUE(nothing.ok);
  EXPECT_EQ(nothing.result["redone"], false);

  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "Tmp"}}})).ok);
  const std::string after = MapText(fixture);

  const ToolResult undone = fixture.CallTool("undo");
  ASSERT_TRUE(undone.ok) << undone.result.dump();
  EXPECT_EQ(undone.result["undone"], true);
  EXPECT_EQ(undone.result["can_undo"], false);
  EXPECT_EQ(undone.result["can_redo"], true);
  EXPECT_EQ(MapText(fixture), before);

  const ToolResult redone = fixture.CallTool("redo");
  ASSERT_TRUE(redone.ok) << redone.result.dump();
  EXPECT_EQ(redone.result["redone"], true);
  EXPECT_EQ(redone.result["can_undo"], true);
  EXPECT_EQ(redone.result["can_redo"], false);
  EXPECT_EQ(MapText(fixture), after);
}

TEST(McpToolsSceneWriteTest, ABatchDoesNotMergeIntoAnOpenEdit) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  // A coalesced edit left open by the UI (a drag in progress) must stay its
  // own undo step.
  MapNode* player = scene.GetMap().FindNode("Player");
  ASSERT_NE(player, nullptr);
  const std::string original = MapText(fixture);
  player->FindComponent("Transform")
      ->properties.Set("position", glm::vec3(9, 9, 9));
  scene.MarkChanged(true, /*coalesced=*/true);
  const std::string dragged = MapText(fixture);

  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "N"}}})).ok);
  scene.Undo();
  EXPECT_EQ(MapText(fixture), dragged) << "the batch alone was undone";
  scene.Undo();
  EXPECT_EQ(MapText(fixture), original);
}

// ---------------------------------------------------------------- atomicity --

TEST(McpToolsSceneWriteTest, AStaticallyBadOperationAppliesNothing) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  const std::string before = MapText(fixture);
  const uint64_t map_rev = scene.MapRevision();
  const uint64_t hierarchy_rev = scene.HierarchyRevision();

  const ToolResult result = Apply(
      fixture,
      json::array({json{{"op", "create"}, {"name", "Fine"}},
                   json{{"op", "set_transform"},
                        {"node", json{{"path", "RollABall/Player"}}},
                        {"position", json::array({1, 1, 1})}},
                   json{{"op", "add_component"},
                        {"node", json{{"path", "RollABall/Player"}}},
                        {"type", "Rigidbdy"}}}));
  ASSERT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[2]"), std::string::npos)
      << ErrorOf(result);
  EXPECT_EQ(result.result["suggestion"], "Rigidbody");
  EXPECT_EQ(result.result["operation"], 2);
  EXPECT_EQ(MapText(fixture), before);
  EXPECT_FALSE(scene.IsDirty());
  EXPECT_FALSE(scene.CanUndo());
  EXPECT_EQ(scene.MapRevision(), map_rev);
  EXPECT_EQ(scene.HierarchyRevision(), hierarchy_rev);
}

TEST(McpToolsSceneWriteTest, AFailureWhileApplyingRestoresTheMap) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "Prior"}}}))
          .ok);
  const std::string before = MapText(fixture);
  const json player_before = NodeAt(fixture, "RollABall/Player");
  const uint64_t map_rev = scene.MapRevision();
  const uint64_t hierarchy_rev = scene.HierarchyRevision();

  // Everything passes the up-front checks; the last operation can only fail
  // against the live map (a node cannot move under its own descendant), after
  // four others changed it.
  const ToolResult result = Apply(
      fixture,
      json::array(
          {json{{"op", "create"}, {"name", "Temp"}, {"id", "t"}},
           json{{"op", "set_transform"},
                {"node", json{{"path", "RollABall/Player"}}},
                {"position", json::array({7, 7, 7})}},
           json{{"op", "delete"},
                {"node", json{{"path", "RollABall/Pickups/Pickup2"}}}},
           json{{"op", "reparent"},
                {"node", json{{"path", "RollABall/Walls"}}},
                {"new_parent", json{{"path", "RollABall/Walls/North"}}}}}));
  ASSERT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[3] (reparent)"), std::string::npos)
      << ErrorOf(result);
  EXPECT_EQ(MapText(fixture), before) << "the map must be byte-identical";
  // The nodes are the same nodes: a client's uids are still valid.
  const json player_after = NodeAt(fixture, "RollABall/Player");
  EXPECT_EQ(player_after["uid"], player_before["uid"]);
  EXPECT_EQ(scene.MapRevision(), map_rev) << "the editor was never notified";
  EXPECT_EQ(scene.HierarchyRevision(), hierarchy_rev);

  // The history is what it was: the one earlier step, nothing half-applied.
  scene.Undo();
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Prior").is_null());
  EXPECT_FALSE(scene.CanUndo());
}

TEST(McpToolsSceneWriteTest, ReadOnlyEditorRefusesTheBatch) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);
  const ToolResult result = fixture.CallTool(
      "apply_scene_operations",
      json{{"operations", json::array({json{{"op", "create"}, {"name", "X"}}})}},
      /*allow_edits=*/false);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(MapText(fixture), before);
  // The same gate for the other write tools.
  EXPECT_FALSE(fixture.CallTool("save_map", json::object(), false).ok);
  EXPECT_FALSE(fixture.CallTool("undo", json::object(), false).ok);
  EXPECT_FALSE(fixture.CallTool("redo", json::object(), false).ok);
  // Selection is UI state: allowed.
  EXPECT_TRUE(fixture
                  .CallTool("select_node",
                            json{{"node", json{{"path", "RollABall/Player"}}}},
                            false)
                  .ok);
}

// ------------------------------------------------------------------ aliases --

TEST(McpToolsSceneWriteTest, AliasesLinkLaterOperationsToEarlierCreations) {
  McpToolFixture fixture;
  const ToolResult result = Apply(
      fixture,
      json::array(
          {json{{"op", "create"}, {"name", "Group"}, {"id", "group"}},
           json{{"op", "create"},
                {"name", "Child"},
                {"parent", json{{"alias", "group"}}},
                {"id", "child"}},
           json{{"op", "add_component"},
                {"node", json{{"alias", "child"}}},
                {"type", "MeshRenderer"},
                {"props", json{{"mesh", "capsule"}}}},
           json{{"op", "set_property"},
                {"node", json{{"alias", "child"}}},
                {"component", "MeshRenderer"},
                {"field", "visible"},
                {"value", false}},
           json{{"op", "set_transform"},
                {"node", json{{"alias", "group"}}},
                {"position", json::array({0, 4, 0})}}}),
      json{{"alias", "child"}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["created"][0]["alias"], "group");
  EXPECT_EQ(result.result["created"][1]["alias"], "child");
  EXPECT_EQ(result.result["created"][1]["path"], "RollABall/Group/Child");
  const json child = NodeAt(fixture, "RollABall/Group/Child");
  EXPECT_EQ(PropsOf(child, "MeshRenderer")["mesh"], "capsule");
  EXPECT_EQ(PropsOf(child, "MeshRenderer")["visible"], false);
  EXPECT_TRUE(Near(NodeAt(fixture, "RollABall/Group")["transform"]["position"],
                   {0, 4, 0}));
  // `select` took the alias.
  EXPECT_EQ(fixture.Scene().GetSelectedUid(), child["uid"].get<uint64_t>());
  EXPECT_EQ(result.result["selected"], child["uid"]);
}

TEST(McpToolsSceneWriteTest, AliasMistakesAreRejectedUpFront) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);
  // Used before it is defined.
  ToolResult result = Apply(
      fixture, json::array({json{{"op", "delete"}, {"node", json{{"alias", "late"}}}},
                            json{{"op", "create"}, {"name", "X"}, {"id", "late"}}}));
  EXPECT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[0]"), std::string::npos);
  EXPECT_NE(ErrorOf(result).find("alias \"late\""), std::string::npos);

  // A typo gets a suggestion.
  result = Apply(
      fixture,
      json::array({json{{"op", "create"}, {"name", "X"}, {"id", "crate"}},
                   json{{"op", "delete"}, {"node", json{{"alias", "crat"}}}}}));
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "crate");

  // Two nodes cannot share an alias.
  result = Apply(
      fixture,
      json::array({json{{"op", "create"}, {"name", "X"}, {"id", "same"}},
                   json{{"op", "create"}, {"name", "Y"}, {"id", "same"}}}));
  EXPECT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[1]"), std::string::npos);

  // An alias whose node an earlier operation deleted.
  result = Apply(
      fixture,
      json::array({json{{"op", "create"}, {"name", "X"}, {"id", "x"}},
                   json{{"op", "delete"}, {"node", json{{"alias", "x"}}}},
                   json{{"op", "set_transform"},
                        {"node", json{{"alias", "x"}}},
                        {"position", json::array({1, 1, 1})}}}));
  EXPECT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[2]"), std::string::npos)
      << ErrorOf(result);
  EXPECT_EQ(MapText(fixture), before);
}

// -------------------------------------------------------- every field type --

TEST(McpToolsSceneWriteTest, EveryFieldTypeGuard) {
  std::vector<bool> seen(static_cast<size_t>(FieldType::kEvent) + 1, false);
  for (const FieldCase& entry : EveryFieldCase()) {
    seen[static_cast<size_t>(entry.field.type)] = true;
  }
  for (size_t i = 0; i < seen.size(); i++) {
    EXPECT_TRUE(seen[i]) << "FieldType " << i << " has no set_property case";
  }
}

TEST(McpToolsSceneWriteTest, SetPropertyTakesEveryFieldType) {
  McpToolFixture fixture;
  RegisterEveryFieldComponent();
  const json target = json{{"path", "RollABall/Walls"}};

  ASSERT_TRUE(Apply(fixture, json::array({json{{"op", "add_component"},
                                               {"node", target},
                                               {"type", "McpEveryField"}}}))
                  .ok);
  json operations = json::array();
  for (const FieldCase& entry : EveryFieldCase()) {
    operations.push_back(json{{"op", "set_property"},
                              {"node", target},
                              {"component", "McpEveryField"},
                              {"field", entry.field.name},
                              {"value", entry.value}});
  }
  const ToolResult result = Apply(fixture, operations);
  ASSERT_TRUE(result.ok) << result.result.dump();

  const json props = PropsOf(NodeAt(fixture, "RollABall/Walls"), "McpEveryField");
  ASSERT_TRUE(props.is_object());
  for (const FieldCase& entry : EveryFieldCase()) {
    ASSERT_TRUE(props.contains(entry.field.name)) << entry.field.name;
    EXPECT_EQ(props[entry.field.name], entry.value)
        << entry.field.name << ": " << props[entry.field.name].dump();
  }

  // The batch was one undo step: it keeps the add_component of the first call.
  fixture.Scene().Undo();
  EXPECT_TRUE(PropsOf(NodeAt(fixture, "RollABall/Walls"), "McpEveryField")
                  .empty());

  // A value equal to the field's default removes the stored property.
  ASSERT_TRUE(Apply(fixture,
                    json::array({json{{"op", "set_property"},
                                      {"node", target},
                                      {"component", "McpEveryField"},
                                      {"field", "count"},
                                      {"value", 4}},
                                 json{{"op", "set_property"},
                                      {"node", target},
                                      {"component", "McpEveryField"},
                                      {"field", "count"},
                                      {"value", 0}}}))
                  .ok);
  EXPECT_FALSE(PropsOf(NodeAt(fixture, "RollABall/Walls"), "McpEveryField")
                   .contains("count"));
  TypeRegistry::Instance().ClearSchemaScripts();
}

TEST(McpToolsSceneWriteTest, BadValuesNameTheOperationAndTheField) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);
  const json player = json{{"path", "RollABall/Player"}};
  struct Bad {
    const char* component;
    const char* field;
    json value;
    const char* expect;
  };
  const Bad cases[] = {
      {"Rigidbody", "mass", "heavy", "expected a finite number"},
      {"Rigidbody", "use_gravity", 1, "expected true or false"},
      {"MeshRenderer", "mesh", "spere", "did you mean"},
      {"MeshRenderer", "color", json::array({1, 2}), "[r, g, b, a]"},
      {"MeshRenderer", "material", json{{"res", "res://nope.masset"}},
       "does not exist"},
      {"Transform", "position", json::array({1, 2}), "[x, y, z]"},
  };
  for (const Bad& bad : cases) {
    const ToolResult result = Apply(
        fixture, json::array({json{{"op", "set_transform"},
                                   {"node", player},
                                   {"position", json::array({1, 1, 1})}},
                              json{{"op", "set_property"},
                                   {"node", player},
                                   {"component", bad.component},
                                   {"field", bad.field},
                                   {"value", bad.value}}}));
    ASSERT_FALSE(result.ok) << bad.field;
    const std::string error = ErrorOf(result);
    EXPECT_NE(error.find("operations[1] (set_property)"), std::string::npos)
        << error;
    EXPECT_NE(error.find(bad.field), std::string::npos) << error;
    EXPECT_NE(error.find(bad.expect), std::string::npos) << error;
    EXPECT_EQ(MapText(fixture), before) << bad.field;
  }
}

// ------------------------------------------------------------ error details --

TEST(McpToolsSceneWriteTest, UnknownNamesCarrySuggestionsAndTheOperationIndex) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);
  const json player = json{{"path", "RollABall/Player"}};
  const json ok_op = json{{"op", "create"}, {"name", "Ok"}};

  struct Case {
    json op;
    std::string suggestion;
    std::string what;
  };
  const std::vector<Case> cases = {
      // component type
      {json{{"op", "add_component"}, {"node", player}, {"type", "Rigidbdy"}},
       "Rigidbody", "type"},
      {json{{"op", "create"}, {"name", "N"},
            {"components", json::array({json{{"type", "MeshRendrer"}}})}},
       "MeshRenderer", "create type"},
      // field
      {json{{"op", "set_property"}, {"node", player}, {"component", "Rigidbody"},
            {"field", "mas"}, {"value", 2}},
       "mass", "field"},
      {json{{"op", "add_component"}, {"node", player}, {"type", "Rigidbody"},
            {"props", json{{"gravity_facto", 2}}}},
       "gravity_factor", "props"},
      // node by path and by uid
      {json{{"op", "delete"}, {"node", json{{"path", "RollABall/Pickups/Pickup"}}}},
       "RollABall/Pickups/Pickup1", "node path"},
      {json{{"op", "set_transform"}, {"node", json{{"path", "RollABall/Plyer"}}},
            {"position", json::array({0, 0, 0})}},
       "RollABall/Player", "set_transform node"},
      {json{{"op", "reparent"}, {"node", player},
            {"new_parent", json{{"path", "RollABall/Wals"}}}},
       "RollABall/Walls", "new_parent"},
      // operation names and fields
      {json{{"op", "creat"}, {"name", "N"}}, "create", "op"},
      {json{{"op", "create"}, {"name", "N"}, {"paret", player}}, "parent",
       "field"},
      // component the node lacks
      {json{{"op", "set_property"}, {"node", player}, {"component", "Rigidbdy"},
            {"field", "mass"}, {"value", 2}},
       "Rigidbody", "set_property component"},
      {json{{"op", "remove_component"}, {"node", player}, {"type", "Rigidbdy"}},
       "Rigidbody", "remove_component"},
  };
  for (const Case& entry : cases) {
    const ToolResult result =
        Apply(fixture, json::array({ok_op, entry.op}));
    ASSERT_FALSE(result.ok) << entry.what;
    EXPECT_NE(ErrorOf(result).find("operations[1]"), std::string::npos)
        << entry.what << ": " << ErrorOf(result);
    EXPECT_EQ(result.result.value("suggestion", std::string()), entry.suggestion)
        << entry.what << ": " << result.result.dump();
    EXPECT_EQ(MapText(fixture), before) << entry.what;
  }
}

TEST(McpToolsSceneWriteTest, MalformedBatchesAreRefused) {
  McpToolFixture fixture;
  EXPECT_FALSE(fixture.CallTool("apply_scene_operations", json::object()).ok);
  EXPECT_FALSE(
      fixture
          .CallTool("apply_scene_operations", json{{"operations", json::array()}})
          .ok);
  EXPECT_FALSE(fixture
                   .CallTool("apply_scene_operations",
                             json{{"operations", "create"}})
                   .ok);
  ToolResult result = fixture.CallTool(
      "apply_scene_operations",
      json{{"operations", json::array({json{{"op", "create"}, {"name", "A"}}})},
           {"selct", json{{"path", "RollABall"}}}});
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["suggestion"], "select");

  json many = json::array();
  for (int i = 0; i < 1001; i++) many.push_back(json{{"op", "create"}, {"name", "N"}});
  result = Apply(fixture, many);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("1000"), std::string::npos) << ErrorOf(result);

  // Wrong shapes inside an operation.
  for (const json& bad : std::vector<json>{
           json::array({"create"}),
           json::array({json{{"name", "A"}}}),
           json::array({json{{"op", "create"}}}),
           json::array({json{{"op", "create"}, {"name", ""}}}),
           json::array({json{{"op", "delete"}, {"node", "RollABall"}}}),
           json::array({json{{"op", "delete"}, {"node", json::object()}}}),
           json::array({json{{"op", "delete"}, {"node", json{{"uid", 1}, {"path", "x"}}}}}),
           json::array({json{{"op", "set_transform"},
                             {"node", json{{"path", "RollABall"}}}}}),
           json::array({json{{"op", "set_property"},
                             {"node", json{{"path", "RollABall"}}},
                             {"component", "Transform"},
                             {"field", "position"}}}),
           json::array({json{{"op", "reparent"},
                             {"node", json{{"path", "RollABall/Player"}}},
                             {"new_parent", json{{"path", "RollABall"}}},
                             {"keep_world_transform", "yes"}}}),
           json::array({json{{"op", "create"}, {"name", "A"},
                             {"components", json::array({json{{"type", "Transform"}}})}}}),
           json::array({json{{"op", "create"}, {"name", "A"},
                             {"components", json::array({json{{"type", "BoxCollider"}},
                                                         json{{"type", "BoxCollider"}}})}}}),
       }) {
    result = Apply(fixture, bad);
    EXPECT_FALSE(result.ok) << bad.dump();
    EXPECT_NE(ErrorOf(result).find("operations[0]"), std::string::npos)
        << bad.dump() << " -> " << ErrorOf(result);
  }
}

// -------------------------------------------------------------- delete etc. --

TEST(McpToolsSceneWriteTest, DeleteRemovesTheSubtreeAndFixesTheSelection) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  MapNode* north = scene.GetMap().FindNode("Walls/North");
  ASSERT_NE(north, nullptr);
  scene.Select(north);
  ASSERT_EQ(scene.GetSelectedUid(), north->uid);

  const ToolResult result = Apply(
      fixture,
      json::array({json{{"op", "delete"}, {"node", json{{"path", "RollABall/Walls"}}}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Walls").is_null());
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Walls/North").is_null());
  EXPECT_EQ(scene.GetSelectedUid(), 0u) << "the selected node was deleted";

  const ToolResult root = Apply(
      fixture,
      json::array({json{{"op", "delete"}, {"node", json{{"path", "RollABall"}}}}}));
  EXPECT_FALSE(root.ok);
  EXPECT_NE(ErrorOf(root).find("root"), std::string::npos);
}

TEST(McpToolsSceneWriteTest, AddAndRemoveComponents) {
  McpToolFixture fixture;
  const json walls = json{{"path", "RollABall/Walls"}};
  ASSERT_TRUE(Apply(fixture, json::array({json{{"op", "add_component"},
                                               {"node", walls},
                                               {"type", "Rigidbody"},
                                               {"props", json{{"mass", 5.0},
                                                              {"use_gravity", false}}}}}))
                  .ok);
  const json props = PropsOf(NodeAt(fixture, "RollABall/Walls"), "Rigidbody");
  ASSERT_TRUE(props.is_object());
  EXPECT_EQ(props["mass"], 5.0);
  EXPECT_EQ(props["use_gravity"], false);

  // A second one of the same type is refused.
  ToolResult again = Apply(fixture, json::array({json{{"op", "add_component"},
                                                      {"node", walls},
                                                      {"type", "Rigidbody"}}}));
  EXPECT_FALSE(again.ok);
  EXPECT_NE(ErrorOf(again).find("already has"), std::string::npos);

  ASSERT_TRUE(Apply(fixture, json::array({json{{"op", "remove_component"},
                                               {"node", walls},
                                               {"type", "Rigidbody"}}}))
                  .ok);
  EXPECT_FALSE(HasComponent(NodeAt(fixture, "RollABall/Walls"), "Rigidbody"));

  // The Transform stays.
  const ToolResult transform = Apply(
      fixture, json::array({json{{"op", "remove_component"},
                                 {"node", walls},
                                 {"type", "Transform"}}}));
  EXPECT_FALSE(transform.ok);
  EXPECT_NE(ErrorOf(transform).find("cannot be removed"), std::string::npos);
  EXPECT_TRUE(HasComponent(NodeAt(fixture, "RollABall/Walls"), "Transform"));
}

// ----------------------------------------------------------------- reparent --

TEST(McpToolsSceneWriteTest, ReparentKeepsTheWorldTransformByDefault) {
  McpToolFixture fixture;
  ASSERT_TRUE(
      Apply(fixture,
            json::array(
                {json{{"op", "create"},
                      {"name", "Pivot"},
                      {"transform", json{{"position", json::array({10, 0, 0})},
                                         {"rotation_euler_degrees",
                                          json::array({0, 90, 0})},
                                         {"scale", json::array({2, 2, 2})}}}},
                 json{{"op", "create"},
                      {"name", "Thing"},
                      {"transform",
                       json{{"position", json::array({1, 2, 3})},
                            {"rotation_euler_degrees", json::array({0, 30, 0})},
                            {"scale", json::array({1, 1, 1})}}}}}))
          .ok);
  const World before = WorldOf(fixture, "RollABall/Thing");

  const ToolResult moved = Apply(
      fixture, json::array({json{{"op", "reparent"},
                                 {"node", json{{"path", "RollABall/Thing"}}},
                                 {"new_parent", json{{"path", "RollABall/Pivot"}}}}}));
  ASSERT_TRUE(moved.ok) << moved.result.dump();
  const World after = WorldOf(fixture, "RollABall/Pivot/Thing");
  EXPECT_LT(glm::length(after.position - before.position), 1e-3f)
      << after.position.x << "," << after.position.y << "," << after.position.z;
  EXPECT_LT(glm::length(after.scale - before.scale), 1e-3f);
  EXPECT_GT(std::abs(glm::dot(after.rotation, before.rotation)), 1.0f - 1e-4f);
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Thing").is_null());

  // One undo puts it all back.
  fixture.Scene().Undo();
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Pivot/Thing").is_null());
  EXPECT_FALSE(NodeAt(fixture, "RollABall/Thing").is_null());
}

TEST(McpToolsSceneWriteTest, ReparentCanKeepTheLocalTransformInstead) {
  McpToolFixture fixture;
  ASSERT_TRUE(
      Apply(fixture,
            json::array(
                {json{{"op", "create"},
                      {"name", "Pivot"},
                      {"transform", json{{"position", json::array({10, 0, 0})}}}},
                 json{{"op", "create"},
                      {"name", "Thing"},
                      {"transform", json{{"position", json::array({1, 2, 3})}}}}}))
          .ok);
  ASSERT_TRUE(
      Apply(fixture,
            json::array({json{{"op", "reparent"},
                              {"node", json{{"path", "RollABall/Thing"}}},
                              {"new_parent", json{{"path", "RollABall/Pivot"}}},
                              {"keep_world_transform", false}}}))
          .ok);
  const json node = NodeAt(fixture, "RollABall/Pivot/Thing");
  EXPECT_TRUE(Near(node["transform"]["position"], {1, 2, 3}));
  const World world = WorldOf(fixture, "RollABall/Pivot/Thing");
  EXPECT_LT(glm::length(world.position - glm::vec3(11, 2, 3)), 1e-3f);
}

TEST(McpToolsSceneWriteTest, ReparentRenamesOnCollisionAndRefusesCycles) {
  McpToolFixture fixture;
  // "North" exists under Walls; a new "North" under the root moves into Walls.
  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "North"}}}))
          .ok);
  const ToolResult moved = Apply(
      fixture, json::array({json{{"op", "reparent"},
                                 {"node", json{{"path", "RollABall/North"}}},
                                 {"new_parent", json{{"path", "RollABall/Walls"}}}}}));
  ASSERT_TRUE(moved.ok) << moved.result.dump();
  EXPECT_FALSE(NodeAt(fixture, "RollABall/Walls/North2").is_null());
  EXPECT_EQ(moved.result["warnings"].size(), 1u) << moved.result.dump();

  const std::string before = MapText(fixture);
  EXPECT_FALSE(Apply(fixture,
                     json::array({json{{"op", "reparent"},
                                       {"node", json{{"path", "RollABall/Walls"}}},
                                       {"new_parent", json{{"path", "RollABall/Walls"}}}}}))
                   .ok);
  EXPECT_FALSE(Apply(fixture,
                     json::array({json{{"op", "reparent"},
                                       {"node", json{{"path", "RollABall"}}},
                                       {"new_parent", json{{"path", "RollABall/Walls"}}}}}))
                   .ok);
  EXPECT_EQ(MapText(fixture), before);
}

// ------------------------------------------------------------------ prefabs --

TEST(McpToolsSceneWriteTest, InstantiatePrefab) {
  McpToolFixture fixture;
  WritePrefab(fixture, "crate.mprefab", kCratePrefab);

  const ToolResult result = Apply(
      fixture,
      json::array(
          {json{{"op", "instantiate_prefab"},
                {"prefab", "res://prefabs/crate.mprefab"},
                {"parent", json{{"path", "RollABall/Walls"}}},
                {"transform", json{{"position", json::array({4, 0, 4})}}},
                {"id", "first"}},
           json{{"op", "instantiate_prefab"},
                {"prefab", "res://prefabs/crate.mprefab"},
                {"name", "Second"}},
           json{{"op", "set_transform"},
                {"node", json{{"alias", "first"}}},
                {"scale", json::array({2, 2, 2})}}}));
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["created"][0]["path"], "RollABall/Walls/crate");
  EXPECT_EQ(result.result["created"][0]["alias"], "first");
  EXPECT_EQ(result.result["created"][1]["path"], "RollABall/Second");

  MapNode* instance = fixture.Scene().GetMap().FindNode("Walls/crate");
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(instance->prefab, "res://prefabs/crate.mprefab");
  ASSERT_EQ(instance->children.size(), 1u);
  EXPECT_EQ(instance->children[0]->name, "Lid");
  EXPECT_TRUE(instance->children[0]->from_prefab);
  const json node = NodeAt(fixture, "RollABall/Walls/crate");
  EXPECT_TRUE(Near(node["transform"]["position"], {4, 0, 4}))
      << "the override replaced the prefab's own position";
  EXPECT_TRUE(Near(node["transform"]["scale"], {2, 2, 2}));
  const json second = NodeAt(fixture, "RollABall/Second");
  EXPECT_TRUE(Near(second["transform"]["position"], {1, 0, 0}))
      << "no override: the prefab's own value";
  EXPECT_FALSE(NodeAt(fixture, "RollABall/Second/Lid").is_null());

  // Saved as instances with overrides only.
  const std::string text = MapText(fixture);
  EXPECT_NE(text.find("instance"), std::string::npos) << text;
  EXPECT_NE(text.find("res://prefabs/crate.mprefab"), std::string::npos);

  fixture.Scene().Undo();
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Second").is_null());
  EXPECT_TRUE(NodeAt(fixture, "RollABall/Walls/crate").is_null());
}

TEST(McpToolsSceneWriteTest, InstantiatePrefabChecksTheFile) {
  McpToolFixture fixture;
  WritePrefab(fixture, "crate.mprefab", kCratePrefab);
  const std::string before = MapText(fixture);
  const json ok_op = json{{"op", "create"}, {"name", "Ok"}};

  // A typo in the file name: a did-you-mean from the project's prefabs.
  ToolResult result = Apply(
      fixture, json::array({ok_op, json{{"op", "instantiate_prefab"},
                                        {"prefab", "res://prefabs/crat.mprefab"}}}));
  ASSERT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("operations[1]"), std::string::npos);
  EXPECT_NE(ErrorOf(result).find("does not exist"), std::string::npos);
  EXPECT_EQ(result.result["suggestion"], "res://prefabs/crate.mprefab");

  // Not a prefab path.
  for (const char* path : {"prefabs/crate.mprefab", "res://maps/main.mmap",
                           "res://../crate.mprefab"}) {
    result = Apply(fixture, json::array({json{{"op", "instantiate_prefab"},
                                              {"prefab", path}}}));
    EXPECT_FALSE(result.ok) << path;
  }

  // A prefab that exists but is not valid.
  WritePrefab(fixture, "broken.mprefab", "this is [not valid");
  result = Apply(fixture, json::array({ok_op, json{{"op", "instantiate_prefab"},
                                                   {"prefab", "res://prefabs/broken.mprefab"}}}));
  ASSERT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("could not be loaded"), std::string::npos)
      << ErrorOf(result);
  EXPECT_EQ(MapText(fixture), before);
}

// ----------------------------------------------------------------- the rest --

TEST(McpToolsSceneWriteTest, SelectNodeTool) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "select_node", json{{"node", json{{"path", "RollABall/Player"}}}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["selected"]["path"], "RollABall/Player");
  EXPECT_EQ(fixture.Scene().GetSelectedUid(),
            result.result["selected"]["uid"].get<uint64_t>());

  const ToolResult by_uid = fixture.CallTool(
      "select_node", json{{"node", json{{"uid", result.result["selected"]["uid"]}}}});
  EXPECT_TRUE(by_uid.ok);

  const ToolResult unknown = fixture.CallTool(
      "select_node", json{{"node", json{{"path", "RollABall/Plyer"}}}});
  EXPECT_FALSE(unknown.ok);
  EXPECT_EQ(unknown.result["suggestion"], "RollABall/Player");

  EXPECT_FALSE(fixture.CallTool("select_node", json::object()).ok);
  // Selecting is not an edit: nothing to undo.
  EXPECT_FALSE(fixture.Scene().CanUndo());
}

TEST(McpToolsSceneWriteTest, SelectInTheBatchIsRefusedForAnUnknownNode) {
  McpToolFixture fixture;
  const std::string before = MapText(fixture);
  const ToolResult result = Apply(
      fixture, json::array({json{{"op", "create"}, {"name", "A"}}}),
      json{{"path", "RollABall/Nope"}});
  ASSERT_FALSE(result.ok);
  EXPECT_NE(ErrorOf(result).find("select"), std::string::npos);
  EXPECT_EQ(MapText(fixture), before) << "the batch is not applied either";
  EXPECT_FALSE(fixture.Scene().CanUndo());
}

TEST(McpToolsSceneWriteTest, SaveMapWritesTheOpenMap) {
  McpToolFixture fixture;
  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "Saved"}}}))
          .ok);
  EXPECT_TRUE(fixture.Scene().IsDirty());

  const ToolResult result = fixture.CallTool("save_map");
  ASSERT_TRUE(result.ok) << result.result.dump();
  EXPECT_EQ(result.result["path"], fixture.Scene().GetMapPath());
  EXPECT_EQ(result.result["path"], "res://maps/main.mmap");
  EXPECT_FALSE(fixture.Scene().IsDirty());

  std::ifstream file(fixture.Root() / "maps" / "main.mmap");
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("name = \"Saved\""), std::string::npos);
  // Saving does not touch the undo history.
  EXPECT_TRUE(fixture.Scene().CanUndo());
}

TEST(McpToolsSceneWriteTest, AppliedBatchNotifiesTheEditorPanels) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  const uint64_t map_rev = scene.MapRevision();
  const uint64_t hierarchy_rev = scene.HierarchyRevision();
  ASSERT_TRUE(
      Apply(fixture, json::array({json{{"op", "create"}, {"name", "N"}}})).ok);
  EXPECT_GT(scene.MapRevision(), map_rev);
  EXPECT_GT(scene.HierarchyRevision(), hierarchy_rev)
      << "the hierarchy panel rebuilds only when this moves";
  EXPECT_TRUE(scene.IsDirty());
}
