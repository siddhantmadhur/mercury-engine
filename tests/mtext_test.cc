// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests for Mercury's TOML text format (format 2), see serialization/mtext.h
// for the layout and the Variant mapping.

#include "project/map.h"
#include "serialization/mtext.h"
#include "version.h"

#include <gtest/gtest.h>

using namespace Mercury;

TEST(MText, ParsesSectionsAttributesAndValues) {
  constexpr const char* kSource = R"([mmap]
format = 2
editor_version = "test"

[[node]]
name = "Ball"
parent = "."

[node.props]
speed = 60.0
count = 3
alive = true
label = "a \"quoted\" \\ string"
position = { vec3 = [1.0, -2.5, 300.0] }
size = { vec2 = [2.0, 2.0] }
tint = { color = [1.0, 0.5, 0.0, 1.0] }
target = { path = "Walls/Left" }
material = { ext = "1" }
texture = { res = "res://ball.png" }
tags = [1, "two", [3.0]]
nothing = { nil = true }
)";
  mtext::Document doc;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(kSource, doc, &error)) << error.ToString();
  ASSERT_EQ(doc.sections.size(), 2u);
  EXPECT_EQ(doc.sections[0].tag, "mmap");
  EXPECT_EQ(doc.sections[0].attributes.GetInt("format"), 2);
  const auto& section = doc.sections[1];
  EXPECT_EQ(section.tag, "node");
  EXPECT_EQ(section.attributes.GetString("name"), "Ball");
  EXPECT_EQ(section.attributes.GetString("parent"), ".");

  const auto& p = section.properties;
  EXPECT_DOUBLE_EQ(p.GetFloat("speed"), 60.0);
  EXPECT_EQ(p.Get("speed")->GetType(), mtext::Variant::Type::kFloat);
  EXPECT_EQ(p.GetInt("count"), 3);
  EXPECT_EQ(p.Get("count")->GetType(), mtext::Variant::Type::kInt);
  EXPECT_TRUE(p.GetBool("alive"));
  EXPECT_EQ(p.GetString("label"), "a \"quoted\" \\ string");
  EXPECT_EQ(*p.Get("position")->Get<glm::vec3>(), glm::vec3(1, -2.5f, 300));
  EXPECT_EQ(*p.Get("size")->Get<glm::vec2>(), glm::vec2(2, 2));
  EXPECT_EQ(p.Get("tint")->Get<mtext::Color>()->rgba, glm::vec4(1, 0.5f, 0, 1));
  EXPECT_EQ(p.Get("target")->Get<mtext::NodePath>()->path, "Walls/Left");
  EXPECT_EQ(p.Get("material")->Get<mtext::ExtResource>()->id, "1");
  EXPECT_EQ(p.Get("texture")->Get<mtext::Resource>()->path, "res://ball.png");
  const auto* tags = p.Get("tags")->Get<mtext::Array>();
  ASSERT_NE(tags, nullptr);
  ASSERT_EQ(tags->size(), 3u);
  EXPECT_EQ(*(*tags)[1].Get<std::string>(), "two");
  EXPECT_TRUE(p.Get("nothing")->IsNil());
}

TEST(MText, WriteThenParseGivesTheSameDocument) {
  mtext::Document doc;
  auto& header = doc.Add("mmap");
  header.attributes.Set("editor_version", MERCURY_VERSION_STRING);
  header.attributes.Set("format", 2);

  auto& ext = doc.Add("ext_resource");
  ext.attributes.Set("id", "1");
  ext.attributes.Set("path", "res://ball.png");
  ext.attributes.Set("type", "Texture");

  auto& node = doc.Add("node");
  node.attributes.Set("name", "Ball");
  node.attributes.Set("parent", ".");
  node.properties.Set("alive", true);
  node.properties.Set("big", 1.0e20);
  node.properties.Set("count", int64_t{3});
  node.properties.Set(
      "items",
      mtext::Array{mtext::Variant(1), mtext::Variant(mtext::NodePath{"."}),
                   mtext::Array{mtext::Variant(2.5)}});
  node.properties.Set("label", "line\nbreak\t\"q\"");
  node.properties.Set("material", mtext::ExtResource{"1"});
  node.properties.Set("nothing", mtext::Variant());
  node.properties.Set("position", glm::vec3(0.5f, -2.0f, 300.0f));
  node.properties.Set("size", glm::vec2(2.0f, 0.25f));
  node.properties.Set("speed", 0.45);
  node.properties.Set("target", mtext::NodePath{"Walls/Left"});
  node.properties.Set("texture", mtext::Resource{"res://ball.png"});
  node.properties.Set("tint", mtext::Color{glm::vec4(1.0f, 0.5f, 0.0f, 0.8f)});
  node.properties.Set("whole", 60.0);

  const std::string text = mtext::Write(doc);
  EXPECT_NE(text.find("[mmap]\n"), std::string::npos) << text;
  EXPECT_NE(text.find("format = 2\n"), std::string::npos) << text;
  EXPECT_NE(text.find("editor_version = \"" MERCURY_VERSION_STRING "\"\n"),
            std::string::npos)
      << text;
  EXPECT_NE(text.find("[[node]]\n"), std::string::npos) << text;
  EXPECT_NE(text.find("[node.props]\n"), std::string::npos) << text;
  EXPECT_NE(text.find("position = { vec3 = [0.5, -2.0, 300.0] }"),
            std::string::npos)
      << text;
  EXPECT_NE(text.find("material = { ext = \"1\" }"), std::string::npos) << text;
  EXPECT_NE(text.find("nothing = { nil = true }"), std::string::npos) << text;

  mtext::Document reparsed;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(text, reparsed, &error)) << error.ToString();
  EXPECT_EQ(reparsed, doc);
  // Saving again is stable: the same document writes the same bytes.
  EXPECT_EQ(mtext::Write(reparsed), text);
}

TEST(MText, WriterIsCanonical) {
  // An interleaved document with unsorted keys: the writer groups sections
  // by tag (alphabetically, keeping each tag's order) and sorts keys, so
  // write -> read -> write is a fixed point.
  mtext::Document doc;
  auto& header = doc.Add("mmap");
  header.attributes.Set("format", 1);
  auto& node = doc.Add("node");
  node.attributes.Set("parent", ".");
  node.attributes.Set("name", "Ball");
  auto& component = doc.Add("component");
  component.attributes.Set("type", "Transform");
  component.properties.Set("speed", 0.45);
  component.properties.Set("alive", true);
  auto& ext = doc.Add("ext_resource");
  ext.attributes.Set("path", "res://a.png");
  ext.attributes.Set("id", "1");
  auto& walls = doc.Add("node");
  walls.attributes.Set("name", "Walls");
  walls.attributes.Set("parent", ".");

  const std::string once = mtext::Write(doc);
  // Sections grouped alphabetically after the header, keys sorted.
  EXPECT_NE(once.find("[[component]]"), std::string::npos) << once;
  EXPECT_LT(once.find("[[component]]"), once.find("[[ext_resource]]"));
  EXPECT_LT(once.find("[[ext_resource]]"), once.find("[[node]]"));
  EXPECT_NE(once.find("name = \"Ball\"\nparent = \".\"\n"), std::string::npos)
      << once;

  mtext::Document reparsed;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(once, reparsed, &error)) << error.ToString();
  EXPECT_EQ(mtext::Write(reparsed), once);
}

TEST(MText, FloatFieldsPrintAtFloatPrecision) {
  // 0.45f is 0.449999988... as a double; it should still be written "0.45".
  // Floats always carry a decimal point.
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(0.45f)), "0.45");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(0.1)), "0.1");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(-0.0)), "0.0");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(glm::vec3(-0.0f, 1, 2))),
            "{ vec3 = [0.0, 1.0, 2.0] }");
}

TEST(MText, ReportsErrorsWithLineNumbers) {
  mtext::Document doc;
  mtext::ParseError error;
  EXPECT_FALSE(mtext::Parse("[mmap]\nformat = 2\nbad =\n", doc, &error));
  EXPECT_EQ(error.line, 3);
  EXPECT_FALSE(
      mtext::Parse("[mmap]\nformat = 2\ns = \"unterminated\n", doc, &error));
  EXPECT_EQ(error.line, 3);
}

TEST(MText, TaggedTablesAreStrict) {
  mtext::Document doc;
  mtext::ParseError error;
  // A tagged inline table carries exactly one key, reported with position.
  EXPECT_FALSE(mtext::Parse(
      "[mmap]\nformat = 2\nx = { vec3 = [1.0, 2.0, 3.0], foo = 1 }\n", doc,
      &error));
  EXPECT_EQ(error.line, 3);
  EXPECT_EQ(error.column, 5);
  EXPECT_FALSE(
      mtext::Parse("[mmap]\nformat = 2\nx = { nil = false }\n", doc, &error));
  EXPECT_EQ(error.line, 3);
  EXPECT_NE(error.message.find("nil"), std::string::npos);
  EXPECT_FALSE(mtext::Parse("[mmap]\nformat = 2\nx = {}\n", doc, &error));
  EXPECT_EQ(error.line, 3);
  EXPECT_NE(error.message.find("empty"), std::string::npos);
}

TEST(MText, ParseValue) {
  EXPECT_DOUBLE_EQ(*mtext::ParseValue("0.45")->Get<double>(), 0.45);
  EXPECT_EQ(*mtext::ParseValue("-12")->Get<int64_t>(), -12);
  EXPECT_TRUE(mtext::ParseValue("true")->Get<bool>());
  EXPECT_TRUE(mtext::ParseValue("{ nil = true }")->IsNil());
  EXPECT_EQ(*mtext::ParseValue("\"a\\nb\"")->Get<std::string>(), "a\nb");
  auto vec = mtext::ParseValue("{ vec2 = [1, 2] }");
  ASSERT_TRUE(vec);
  EXPECT_EQ(*vec->Get<glm::vec2>(), glm::vec2(1.0f, 2.0f));
  auto res = mtext::ParseValue("{ res = \"res://a.png\" }");
  ASSERT_TRUE(res);
  EXPECT_EQ(res->Get<mtext::Resource>()->path, "res://a.png");
  EXPECT_FALSE(mtext::ParseValue("{ vec3 = [1.0, 2.0] }"));
  EXPECT_FALSE(mtext::ParseValue(""));
  // One literal only: trailing keys or tables are rejected, not ignored.
  EXPECT_FALSE(mtext::ParseValue("1\nx = 2"));
  EXPECT_FALSE(mtext::ParseValue("1\n[t]"));
  auto vec3 = mtext::ParseValue("{ vec3 = [1, 2, 3] }");
  ASSERT_TRUE(vec3);
  EXPECT_EQ(*vec3->Get<glm::vec3>(), glm::vec3(1.0f, 2.0f, 3.0f));
  // Format 1 literals no longer parse.
  EXPECT_FALSE(mtext::ParseValue("Vector3(1, 2, 3)"));
  EXPECT_FALSE(mtext::ParseValue("null"));
}

TEST(MText, ReaderGroupsSectionsByTag) {
  constexpr const char* kSource = R"([mmap]
format = 2
editor_version = "test"

[[component]]
node = "."
type = "Transform"

[component.props]
position = { vec3 = [0.0, 0.0, 0.0] }

[[node]]
name = "Ball"

[node.props]
speed = 60.0

[[node]]
name = "Walls"
parent = "."
)";
  mtext::Document doc;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(kSource, doc, &error)) << error.ToString();
  // The header stays in front; the rest arrives grouped (and sorted) by tag.
  ASSERT_EQ(doc.sections.size(), 4u);
  EXPECT_EQ(doc.sections[0].tag, "mmap");
  EXPECT_EQ(doc.sections[0].attributes.GetInt("format"), 2);
  EXPECT_EQ(doc.sections[1].tag, "component");
  EXPECT_EQ(doc.sections[2].tag, "node");
  EXPECT_EQ(doc.sections[3].tag, "node");
  EXPECT_EQ(doc.sections[3].attributes.GetString("name"), "Walls");
  EXPECT_EQ(doc.sections[3].attributes.GetString("parent"), ".");
  EXPECT_EQ(doc.sections[2].attributes.GetString("name"), "Ball");
  EXPECT_DOUBLE_EQ(doc.sections[2].properties.GetFloat("speed", 0.0), 60.0);
  EXPECT_EQ(*doc.sections[1].properties.Get("position")->Get<glm::vec3>(),
            glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST(MText, ReaderRejectsOldAndNewerFormats) {
  mtext::Document doc;
  mtext::ParseError error;
  // Older than what this editor reads.
  EXPECT_FALSE(mtext::Parse("[mmap]\nformat = 1\n", doc, &error));
  EXPECT_NE(error.message.find("format 1 is not supported"), std::string::npos);
  // Newer than what this editor reads: name the writing editor.
  EXPECT_FALSE(mtext::Parse("[mmap]\nformat = 3\neditor_version = \"1.2.0\"\n",
                            doc, &error));
  EXPECT_NE(error.message.find("format 3 is newer than this editor"),
            std::string::npos);
  EXPECT_NE(error.message.find("written by editor 1.2.0"), std::string::npos);
  EXPECT_NE(error.message.find("reads format 2"), std::string::npos);
  // A newer file without editor_version says so, with the exact message.
  EXPECT_FALSE(mtext::Parse("[mmap]\nformat = 3\n", doc, &error));
  EXPECT_EQ(error.message,
            "format 3 is newer than this editor, which reads format 2");
  // The current format is accepted.
  error = {};
  EXPECT_TRUE(mtext::Parse("[mmap]\nformat = 2\n", doc, &error))
      << error.ToString();
  EXPECT_FALSE(mtext::Parse("[mmap]\n", doc, &error));
  EXPECT_FALSE(mtext::Parse("x = 1\n", doc, &error));
}

// The order-independence fix lives in project/map.cc. Sections arrive in any
// relative order once files are TOML, and map loading must not depend on the
// old interleaving.
TEST(MText, MapLoadsSectionsInAnyOrder) {
  Map map;
  std::string error;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "."
type = "Transform"

[component.props]
position = { vec3 = [1.0, 2.0, 3.0] }

[[node]]
name = "Ball"

[[node]]
name = "Walls"
parent = "."

[[component]]
node = "Walls"
type = "MeshRenderer"

[component.props]
texture = { ext = "1" }

[[ext_resource]]
id = "1"
path = "res://mat.png"
type = "Texture"
)",
                                 &error))
      << error;
  const MapNode* root = map.GetRoot();
  ASSERT_NE(root, nullptr);
  EXPECT_EQ(root->name, "Ball");
  const ComponentData* transform = root->FindComponent("Transform", 0);
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(*transform->properties.Get("position")->Get<glm::vec3>(),
            glm::vec3(1.0f, 2.0f, 3.0f));
  ASSERT_EQ(root->children.size(), 1u);
  EXPECT_EQ(root->children[0]->name, "Walls");
  const ComponentData* mesh =
      root->children[0]->FindComponent("MeshRenderer", 0);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->properties.Get("texture")->Get<mtext::Resource>()->path,
            "res://mat.png");
}
