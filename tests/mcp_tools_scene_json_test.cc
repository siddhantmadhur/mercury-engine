// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the MCP scene helpers (editor/mcp/tools/scene_json.h): node refs by
// uid and path, a node as JSON, and the JSON -> property value inverse, on a
// small in-memory map.

#include "mcp/tools/scene_json.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "introspection/variant_json.h"
#include "mcp_tools_fixture.h"
#include "project/project.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

namespace fs = std::filesystem;
using namespace Mercury;
using Mercury::mcp::JsonToPropertyValue;
using Mercury::mcp::NodeToJson;
using Mercury::mcp::ResolveNode;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace {

fs::path TempDir(const char* name) {
  const fs::path dir =
      fs::temp_directory_path() /
      (std::string(name) + "_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  return dir;
}

// An EditorScene whose settings live in a throwaway config dir.
struct TestScene {
  TestScene() : config(TempDir("mcp_scene_json_cfg")) {
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    previous_ = previous ? previous : "";
    setenv("XDG_CONFIG_HOME", config.string().c_str(), 1);
    // Root -> Child -> Grandchild, Root -> Other
    Map& map = scene.GetMap();
    child = map.AddNode(map.GetRoot(), "Child");
    grandchild = map.AddNode(child, "Grandchild");
    other = map.AddNode(map.GetRoot(), "Other");
  }
  ~TestScene() {
    if (previous_.empty()) {
      unsetenv("XDG_CONFIG_HOME");
    } else {
      setenv("XDG_CONFIG_HOME", previous_.c_str(), 1);
    }
  }

  fs::path config;
  std::string previous_;
  EditorScene scene;
  MapNode* child = nullptr;
  MapNode* grandchild = nullptr;
  MapNode* other = nullptr;
};

FieldInfo MakeField(const char* name, FieldType type, mtext::Variant value) {
  FieldInfo field;
  field.name = name;
  field.type = type;
  field.default_value = std::move(value);
  return field;
}

// One field of every FieldType, each with a non-default value (lists of three
// element types, an enum with values).
std::vector<FieldInfo> EveryField() {
  std::vector<FieldInfo> fields;
  fields.push_back(MakeField("flag", FieldType::kBool, true));
  fields.push_back(MakeField("count", FieldType::kInt, int64_t{-7}));
  fields.push_back(MakeField("speed", FieldType::kFloat, 2.5));
  fields.push_back(MakeField("label", FieldType::kString, std::string("hi")));
  fields.push_back(MakeField("size", FieldType::kVector2, glm::vec2(1.5f, -2)));
  fields.push_back(
      MakeField("offset", FieldType::kVector3, glm::vec3(0.25f, 8, -1)));
  fields.push_back(MakeField(
      "tint", FieldType::kColor, mtext::Color{glm::vec4(1, 0.5f, 0.25f, 0.75f)}));
  fields.push_back(MakeField("target", FieldType::kNodeRef,
                             mtext::NodePath{"Child/Grandchild"}));
  FieldInfo asset = MakeField("material", FieldType::kAsset,
                              mtext::Resource{"res://assets/white.masset"});
  asset.asset_type = "Material";
  fields.push_back(asset);
  FieldInfo mode = MakeField("mode", FieldType::kEnum, std::string("fast"));
  mode.enum_values = {"slow", "fast", "ludicrous"};
  fields.push_back(mode);
  FieldInfo ints = MakeField(
      "ids", FieldType::kList,
      mtext::Array{mtext::Variant(int64_t{1}), mtext::Variant(int64_t{2})});
  ints.element_type = FieldType::kInt;
  fields.push_back(ints);
  FieldInfo names = MakeField(
      "names", FieldType::kList,
      mtext::Array{mtext::Variant(std::string("a")),
                   mtext::Variant(std::string("b"))});
  names.element_type = FieldType::kString;
  fields.push_back(names);
  FieldInfo assets = MakeField(
      "materials", FieldType::kList,
      mtext::Array{mtext::Variant(mtext::Resource{"res://a.masset"})});
  assets.element_type = FieldType::kAsset;
  fields.push_back(assets);
  mtext::Array call{mtext::Variant(mtext::NodePath{"Other"}),
                    mtext::Variant(std::string("Door")),
                    mtext::Variant(std::string("Open")),
                    mtext::Variant(true)};
  fields.push_back(MakeField("on_hit", FieldType::kEvent,
                             mtext::Array{mtext::Variant(std::move(call))}));
  return fields;
}

}  // namespace

TEST(McpSceneJsonTest, EveryFieldTypeIsCovered) {
  // Guards the round-trip test below: a new FieldType must get a field here.
  std::vector<bool> seen(static_cast<size_t>(FieldType::kEvent) + 1, false);
  for (const FieldInfo& field : EveryField()) {
    seen[static_cast<size_t>(field.type)] = true;
  }
  for (size_t i = 0; i < seen.size(); i++) {
    EXPECT_TRUE(seen[i]) << "FieldType " << i << " has no round-trip field";
  }
}

TEST(McpSceneJsonTest, NodeToJsonRoundTripsEveryFieldType) {
  TestScene test;
  const std::vector<FieldInfo> fields = EveryField();
  ComponentData component{"Everything", {}};
  for (const FieldInfo& field : fields) {
    component.properties.Set(field.name, field.default_value);
  }
  test.grandchild->components.push_back(component);

  // Through text, as a client sees it.
  const json node =
      json::parse(NodeToJson(test.scene, *test.grandchild, true).dump());
  const json* props = nullptr;
  for (const json& entry : node["components"]) {
    if (entry["type"] == "Everything") props = &entry["props"];
  }
  ASSERT_NE(props, nullptr) << node.dump(2);

  for (const FieldInfo& field : fields) {
    ASSERT_TRUE(props->contains(field.name)) << field.name;
    mtext::Variant back;
    std::string error;
    ASSERT_TRUE(JsonToPropertyValue(field, (*props)[field.name], back, &error))
        << field.name << ": " << error;
    EXPECT_EQ(back, field.default_value)
        << field.name << " " << (*props)[field.name].dump();
  }
}

TEST(McpSceneJsonTest, NodeToJsonDescribesTheNode) {
  TestScene test;
  ComponentData transform{"Transform", {}};
  transform.properties.Set("position", glm::vec3(1, 2, 3));
  transform.properties.Set("rotation", glm::vec3(0, 90, 0));
  transform.properties.Set("scale", glm::vec3(2, 2, 2));
  test.child->components.push_back(transform);

  const json plain = NodeToJson(test.scene, *test.child, false);
  EXPECT_EQ(plain["name"], "Child");
  EXPECT_EQ(plain["uid"], test.child->uid);
  EXPECT_EQ(plain["path"], "Root/Child");
  EXPECT_EQ(plain["node_path"], "Child");
  EXPECT_EQ(plain["parent_uid"], test.scene.GetMap().GetRoot()->uid);
  EXPECT_EQ(plain["child_count"], 1);
  EXPECT_FALSE(plain.contains("components"));
  EXPECT_EQ(plain["transform"]["position"], json::array({1, 2, 3}));
  EXPECT_EQ(plain["transform"]["euler_degrees"], json::array({0, 90, 0}));
  EXPECT_EQ(plain["transform"]["scale"], json::array({2, 2, 2}));
  // 90 degrees about Y: (0, sin 45, 0, cos 45).
  ASSERT_EQ(plain["transform"]["rotation"].size(), 4u);
  EXPECT_NEAR(plain["transform"]["rotation"][1].get<double>(), 0.70710678, 1e-6);
  EXPECT_NEAR(plain["transform"]["rotation"][3].get<double>(), 0.70710678, 1e-6);

  const json full = NodeToJson(test.scene, *test.child, true);
  ASSERT_EQ(full["components"].size(), 1u);
  EXPECT_EQ(full["components"][0]["type"], "Transform");
  EXPECT_EQ(full["components"][0]["props"]["position"], json::array({1, 2, 3}));

  // A node without a Transform component reports the identity transform; the
  // root has no parent.
  const json root =
      NodeToJson(test.scene, *test.scene.GetMap().GetRoot(), false);
  EXPECT_TRUE(root["parent_uid"].is_null());
  EXPECT_EQ(root["node_path"], ".");
  EXPECT_EQ(root["transform"]["scale"], json::array({1, 1, 1}));
}

TEST(McpSceneJsonTest, ValueEncodingMatchesIntrospection) {
  // The shared helper is the one the schema JSON uses; spot-check its shapes.
  using introspection::VariantToJson;
  EXPECT_EQ(VariantToJson(mtext::Variant(glm::vec3(1, 2, 3))),
            json::array({1, 2, 3}));
  EXPECT_EQ(VariantToJson(mtext::Variant(mtext::Resource{"res://a"})),
            (json{{"res", "res://a"}}));
  EXPECT_EQ(VariantToJson(mtext::Variant(mtext::NodePath{"A/B"})),
            (json{{"path", "A/B"}}));
}

TEST(McpSceneJsonTest, ResolveByUidNumberAndString) {
  TestScene test;
  std::string error;
  EXPECT_EQ(ResolveNode(test.scene, json{{"uid", test.child->uid}}, &error),
            test.child)
      << error;
  EXPECT_EQ(ResolveNode(test.scene,
                        json{{"uid", std::to_string(test.grandchild->uid)}},
                        &error),
            test.grandchild)
      << error;
  // A plain int literal is a signed JSON number; it must work as well.
  EXPECT_EQ(ResolveNode(test.scene,
                        json{{"uid", static_cast<int>(test.other->uid)}}, &error),
            test.other)
      << error;
}

TEST(McpSceneJsonTest, ResolveByPath) {
  TestScene test;
  std::string error;
  EXPECT_EQ(
      ResolveNode(test.scene, json{{"path", "Root/Child/Grandchild"}}, &error),
      test.grandchild)
      << error;
  EXPECT_EQ(ResolveNode(test.scene, json{{"path", "Root"}}, &error),
            test.scene.GetMap().GetRoot());
  // The engine's own form works too.
  EXPECT_EQ(ResolveNode(test.scene, json{{"path", "Child/Grandchild"}}, &error),
            test.grandchild);
  EXPECT_EQ(ResolveNode(test.scene, json{{"path", "."}}, &error),
            test.scene.GetMap().GetRoot());
}

TEST(McpSceneJsonTest, UnknownPathSaysWhichFormFailedAndSuggests) {
  TestScene test;
  std::string error;
  std::string suggestion;
  EXPECT_EQ(ResolveNode(test.scene, json{{"path", "Root/Child/Grandchlid"}},
                        &error, &suggestion),
            nullptr);
  EXPECT_NE(error.find("\"path\""), std::string::npos) << error;
  EXPECT_NE(error.find("Root/Child/Grandchlid"), std::string::npos) << error;
  EXPECT_NE(error.find("did you mean \"Root/Child/Grandchild\""),
            std::string::npos)
      << error;
  EXPECT_EQ(suggestion, "Root/Child/Grandchild");

  // Nothing close: an error without a suggestion.
  EXPECT_EQ(ResolveNode(test.scene, json{{"path", "Zzzzzzzzzzzz"}}, &error,
                        &suggestion),
            nullptr);
  EXPECT_EQ(error.find("did you mean"), std::string::npos) << error;
  EXPECT_TRUE(suggestion.empty());
}

TEST(McpSceneJsonTest, UnknownUidSaysWhichFormFailedAndSuggests) {
  TestScene test;
  std::string error;
  std::string suggestion;
  // Far from every uid: an error naming the form and the uid, no suggestion.
  const uint64_t far_away = 987654321;
  EXPECT_EQ(ResolveNode(test.scene, json{{"uid", far_away}}, &error, &suggestion),
            nullptr);
  EXPECT_NE(error.find("\"uid\""), std::string::npos) << error;
  EXPECT_NE(error.find(std::to_string(far_away)), std::string::npos) << error;
  EXPECT_TRUE(suggestion.empty()) << suggestion;

  // One digit longer than a real uid (as a string): the suggestion is that uid.
  const std::string near_miss = std::to_string(test.other->uid) + "0";
  EXPECT_EQ(ResolveNode(test.scene, json{{"uid", near_miss}}, &error,
                        &suggestion),
            nullptr);
  EXPECT_EQ(suggestion, std::to_string(test.other->uid)) << error;
  EXPECT_NE(error.find("did you mean uid " + std::to_string(test.other->uid)),
            std::string::npos)
      << error;
}

TEST(McpSceneJsonTest, MalformedRefsAreRejectedWithTheValidForms) {
  TestScene test;
  std::string error;
  for (const json& ref :
       {json("Root/Child"), json(42), json::object(),
        json{{"uid", 1}, {"path", "Root"}}, json{{"uid", -1}},
        json{{"uid", "12x"}}, json{{"uid", 1.5}}, json{{"path", ""}},
        json{{"path", 7}}}) {
    EXPECT_EQ(ResolveNode(test.scene, ref, &error), nullptr) << ref.dump();
    EXPECT_NE(error.find(ref.dump()), std::string::npos) << error;
  }
  EXPECT_EQ(ResolveNode(test.scene, json("x"), &error), nullptr);
  EXPECT_NE(error.find("{\"uid\""), std::string::npos) << error;
}

TEST(McpSceneJsonTest, JsonToPropertyValueRejectsBadValues) {
  const std::vector<FieldInfo> fields = EveryField();
  auto field = [&](const char* name) -> const FieldInfo& {
    for (const FieldInfo& f : fields) {
      if (f.name == name) return f;
    }
    ADD_FAILURE() << name;
    return fields.front();
  };
  struct Bad {
    const char* field;
    json value;
    const char* error_contains;
  };
  const Bad bad[] = {
      {"flag", json(1), "true or false"},
      {"count", json(1.5), "integer"},
      {"count", json("3"), "integer"},
      {"speed", json("fast"), "number"},
      {"label", json(3), "string"},
      {"size", json::array({1, 2, 3}), "[x, y]"},
      {"offset", json::array({1, 2}), "[x, y, z]"},
      {"offset", json::array({1, 2, "z"}), "[x, y, z]"},
      {"tint", json::array({1, 1, 1}), "[r, g, b, a]"},
      {"target", json(5), "path"},
      {"material", json{{"res", "assets/x.masset"}}, "res://"},
      {"material", json(5), "res://"},
      {"mode", json("ludicrus"), "did you mean \"ludicrous\""},
      {"mode", json(1), "enum"},
      {"ids", json("x"), "array"},
      {"ids", json::array({1, "two"}), "[1]"},
      {"on_hit", json::array({json::array({"Other", "Door"})}), "call 0"},
  };
  for (const Bad& b : bad) {
    mtext::Variant out = mtext::Variant(std::string("untouched"));
    std::string error;
    EXPECT_FALSE(JsonToPropertyValue(field(b.field), b.value, out, &error))
        << b.field << " " << b.value.dump();
    EXPECT_NE(error.find(std::string("field \"") + b.field + "\""),
              std::string::npos)
        << error;
    EXPECT_NE(error.find(b.error_contains), std::string::npos) << error;
    EXPECT_EQ(out, mtext::Variant(std::string("untouched")));
  }
}

TEST(McpSceneJsonTest, JsonToPropertyValueAcceptsTheLenientForms) {
  const std::vector<FieldInfo> fields = EveryField();
  auto convert = [&](size_t index, const json& value) {
    mtext::Variant out;
    std::string error;
    EXPECT_TRUE(JsonToPropertyValue(fields[index], value, out, &error))
        << fields[index].name << ": " << error;
    return out;
  };
  // fields: 1 count, 2 speed, 5 offset, 7 target, 8 material
  EXPECT_EQ(convert(1, json(3.0)), mtext::Variant(int64_t{3}));
  EXPECT_EQ(convert(2, json(3)), mtext::Variant(3.0));
  EXPECT_EQ(convert(5, json{{"vec3", json::array({1, 2, 3})}}),
            mtext::Variant(glm::vec3(1, 2, 3)));
  EXPECT_EQ(convert(7, json("Child")), mtext::Variant(mtext::NodePath{"Child"}));
  EXPECT_EQ(convert(7, json(nullptr)), mtext::Variant(mtext::NodePath{}));
  EXPECT_EQ(convert(8, json("")), mtext::Variant(mtext::Resource{}));
}

TEST(McpSceneJsonTest, ResourceFieldsMustExistInTheProject) {
  const fs::path root = TempDir("mcp_scene_json_project");
  auto project = Project::Create(root / "game.mproj", "Game");
  ASSERT_NE(project, nullptr);
  fs::create_directories(root / "assets");
  std::ofstream(root / "assets" / "white.masset") << "x";

  FieldInfo field = MakeField("material", FieldType::kAsset, mtext::Resource{});
  mtext::Variant out;
  std::string error;
  EXPECT_TRUE(JsonToPropertyValue(
      field, json{{"res", "res://assets/white.masset"}}, out, &error,
      project.get()))
      << error;
  EXPECT_EQ(out, mtext::Variant(mtext::Resource{"res://assets/white.masset"}));

  EXPECT_FALSE(JsonToPropertyValue(
      field, json{{"res", "res://assets/nope.masset"}}, out, &error,
      project.get()));
  EXPECT_NE(error.find("does not exist"), std::string::npos) << error;
  EXPECT_FALSE(JsonToPropertyValue(
      field, json{{"res", "res://../outside.masset"}}, out, &error,
      project.get()));
  // Unset always passes; without a project the check is skipped.
  EXPECT_TRUE(
      JsonToPropertyValue(field, json{{"res", ""}}, out, &error, project.get()));
  EXPECT_TRUE(
      JsonToPropertyValue(field, json{{"res", "res://anything"}}, out, &error));
  fs::remove_all(root);
}

// The shared fixture end to end: a real project opened in a headless editor,
// a real node resolved, serialized and every one of its stored component
// values converted back through the registry's field descriptions.
TEST(McpSceneJsonTest, RealProjectNodesRoundTripThroughTheRegistry) {
  McpToolFixture fixture;
  EditorScene& scene = fixture.Scene();
  ASSERT_NE(scene.GetProject(), nullptr);

  std::string error;
  MapNode* player =
      ResolveNode(scene, json{{"path", "RollABall/Player"}}, &error);
  ASSERT_NE(player, nullptr) << error;
  EXPECT_EQ(ResolveNode(scene, json{{"uid", std::to_string(player->uid)}}, &error),
            player);
  EXPECT_EQ(ResolveNode(scene, json{{"path", "Player"}}, &error), player);

  const json node = json::parse(NodeToJson(scene, *player, true).dump());
  EXPECT_EQ(node["path"], "RollABall/Player");
  EXPECT_EQ(node["transform"]["position"], json::array({0, 0.5, 0}));
  ASSERT_GE(node["components"].size(), 4u) << node.dump(2);

  const Project* project = scene.GetProject().get();
  size_t checked = 0;
  for (const json& component : node["components"]) {
    const std::string type = component["type"].get<std::string>();
    const ComponentInfo* info = TypeRegistry::Instance().FindComponent(type);
    if (!info) continue;  // a script the editor has no schema for
    for (const auto& [name, value] : component["props"].items()) {
      const FieldInfo* field = info->FindField(name);
      ASSERT_NE(field, nullptr) << type << "." << name;
      mtext::Variant back;
      ASSERT_TRUE(JsonToPropertyValue(*field, value, back, &error, project))
          << type << "." << name << ": " << error;
      const mtext::Variant* original =
          player->FindComponent(type)->properties.Get(name);
      ASSERT_NE(original, nullptr);
      EXPECT_EQ(back, *original) << type << "." << name;
      checked++;
    }
  }
  EXPECT_GE(checked, 3u);  // at least position, material, mesh

  // The material reference was resolved from the file's ExtResource to its
  // res:// path, which exists in the project copy.
  bool saw_material = false;
  for (const json& component : node["components"]) {
    if (component["type"] == "MeshRenderer") {
      EXPECT_EQ(component["props"]["material"],
                (json{{"res", "res://assets/player.masset"}}));
      saw_material = true;
    }
  }
  EXPECT_TRUE(saw_material);

  // A missing node gets a did-you-mean from the real tree.
  std::string suggestion;
  EXPECT_EQ(ResolveNode(scene, json{{"path", "RollABall/Pickups/Pickup9"}},
                        &error, &suggestion),
            nullptr);
  EXPECT_FALSE(suggestion.empty());
  EXPECT_NE(suggestion.find("Pickup"), std::string::npos) << suggestion;
}
