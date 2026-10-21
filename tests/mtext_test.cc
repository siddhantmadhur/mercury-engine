#include <gtest/gtest.h>

#include "serialization/mtext.h"

using namespace Mercury;

TEST(MText, ParsesSectionsAttributesAndValues) {
  constexpr const char* kSource = R"([node name="Ball" parent="."]
speed = 60.0
count = 3
alive = true
label = "a \"quoted\" \\ string"
position = Vector3(1, -2.5, 3e2)
size = Vector2(2, 2)
tint = Color(1, 0.5, 0)
target = NodePath("Walls/Left")
material = ExtResource("1")
texture = Resource("res://ball.png")
tags = [1, "two", [3.0]]
nothing = null ; trailing comment
)";
  mtext::Document doc;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(kSource, doc, &error)) << error.ToString();
  ASSERT_EQ(doc.sections.size(), 1u);
  const auto& section = doc.sections[0];
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
  header.attributes.Set("format", 1);
  auto& a = doc.Add("ext_resource");
  a.attributes.Set("id", "1");
  auto& b = doc.Add("ext_resource");
  b.attributes.Set("id", "2");
  auto& node = doc.Add("component");
  node.attributes.Set("type", "Ball");
  node.properties.Set("speed", 0.45);
  node.properties.Set("whole", 60.0);
  node.properties.Set("big", 1.0e20);
  node.properties.Set("text", "line\nbreak\t\"q\"");
  node.properties.Set("v", glm::vec3(0.1f, -0.0f, 1e-7f));
  node.properties.Set("items", mtext::Array{mtext::Variant(1),
                                            mtext::Variant(mtext::NodePath{"."})});

  const std::string text = mtext::Write(doc);
  // Floats keep a decimal point and print at the precision they came from.
  EXPECT_NE(text.find("speed = 0.45\n"), std::string::npos) << text;
  EXPECT_NE(text.find("whole = 60.0\n"), std::string::npos) << text;
  // Consecutive [ext_resource] lines stay together.
  EXPECT_NE(text.find("[ext_resource id=\"1\"]\n[ext_resource id=\"2\"]"),
            std::string::npos)
      << text;

  mtext::Document reparsed;
  mtext::ParseError error;
  ASSERT_TRUE(mtext::Parse(text, reparsed, &error)) << error.ToString();
  EXPECT_EQ(reparsed, doc);
  EXPECT_EQ(mtext::Write(reparsed), text);
}

TEST(MText, FloatFieldsPrintAtFloatPrecision) {
  // 0.45f is 0.449999988... as a double; it should still be written "0.45".
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(0.45f)), "0.45");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(0.1)), "0.1");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(-0.0)), "0.0");
  EXPECT_EQ(mtext::WriteValue(mtext::Variant(glm::vec3(-0.0f, 1, 2))),
            "Vector3(0, 1, 2)");
}

TEST(MText, ReportsErrorsWithLineNumbers) {
  mtext::Document doc;
  mtext::ParseError error;
  EXPECT_FALSE(mtext::Parse("[a]\nx = 1\ny = Vector3(1, 2)\n", doc, &error));
  EXPECT_EQ(error.line, 3);

  EXPECT_FALSE(mtext::Parse("x = 1\n", doc, &error));
  EXPECT_EQ(error.line, 1);

  EXPECT_FALSE(mtext::Parse("[a]\nname = \"unterminated\n", doc, &error));
  EXPECT_FALSE(mtext::Parse("[a b=]\n", doc, &error));
  EXPECT_FALSE(mtext::Parse("[a]\nx = Banana(1)\n", doc, &error));
  EXPECT_FALSE(mtext::Parse("[a]\nx = 1 2\n", doc, &error));
}

TEST(MText, ParseValue) {
  auto v = mtext::ParseValue("Color(0.2, 0.4, 0.6, 0.8)");
  ASSERT_TRUE(v);
  EXPECT_EQ(v->Get<mtext::Color>()->rgba, glm::vec4(0.2f, 0.4f, 0.6f, 0.8f));
  EXPECT_FALSE(mtext::ParseValue("Vector2(1)"));
  EXPECT_EQ(*mtext::ParseValue("-12")->Get<int64_t>(), -12);
}
