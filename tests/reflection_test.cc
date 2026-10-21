#include <gtest/gtest.h>

#include "components/components.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

using namespace Mercury;

namespace {

enum class Mode { kA, kB };

struct Everything {
  bool flag = true;
  int count = 3;
  float speed = 1.5f;
  std::string label = "hi";
  glm::vec2 v2 = glm::vec2(1, 2);
  glm::vec3 v3 = glm::vec3(1, 2, 3);
  glm::vec4 color = glm::vec4(1);
  NodeRef target{"Ball"};
  AssetRef material{"res://a.masset"};
  Mode mode = Mode::kB;

  void Reflect(Reflector& r) {
    r.Field("flag", flag);
    r.Field("count", count);
    r.Field("speed", speed).Tooltip("fast");
    r.Field("label", label);
    r.Field("v2", v2);
    r.Field("v3", v3);
    r.Field("color", color);
    r.Field("target", target);
    r.Field("material", material, "Material");
    r.Enum("mode", mode, {"a", "b"});
  }
};

}  // namespace

TEST(Reflection, DescribesEveryFieldType) {
  auto fields = DescribeFields<Everything>();
  ASSERT_EQ(fields.size(), 10u);
  EXPECT_EQ(fields[0].type, FieldType::kBool);
  EXPECT_EQ(fields[2].type, FieldType::kFloat);
  EXPECT_EQ(fields[2].tooltip, "fast");
  EXPECT_EQ(fields[6].type, FieldType::kColor);
  EXPECT_EQ(fields[7].type, FieldType::kNodeRef);
  EXPECT_EQ(fields[8].asset_type, "Material");
  EXPECT_EQ(fields[9].type, FieldType::kEnum);
  EXPECT_EQ(fields[9].enum_values, (std::vector<std::string>{"a", "b"}));
  EXPECT_EQ(*fields[9].default_value.Get<std::string>(), "b");
}

TEST(Reflection, WriteThenReadRestoresValues) {
  Everything original;
  original.flag = false;
  original.count = -7;
  original.speed = 0.45f;
  original.label = "changed";
  original.v3 = glm::vec3(4, 5, 6);
  original.color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
  original.target.path = "Walls/Left";
  original.material.path = "res://b.masset";
  original.mode = Mode::kA;

  mtext::PropertyList properties;
  auto writer = Reflector::Write(properties);
  original.Reflect(writer);

  Everything loaded = ReadFields<Everything>(properties, "test");
  EXPECT_EQ(loaded.flag, false);
  EXPECT_EQ(loaded.count, -7);
  EXPECT_FLOAT_EQ(loaded.speed, 0.45f);
  EXPECT_EQ(loaded.label, "changed");
  EXPECT_EQ(loaded.v3, glm::vec3(4, 5, 6));
  EXPECT_EQ(loaded.color, glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
  EXPECT_EQ(loaded.target.path, "Walls/Left");
  EXPECT_EQ(loaded.material.path, "res://b.masset");
  EXPECT_EQ(loaded.mode, Mode::kA);
}

TEST(Reflection, KeepsDefaultsForMissingOrMistypedProperties) {
  mtext::PropertyList properties;
  properties.Set("count", "not a number");
  properties.Set("mode", "c");
  properties.Set("speed", 2);  // ints are accepted for floats
  Everything instance;
  auto reader = Reflector::Read(properties);
  instance.Reflect(reader);
  EXPECT_EQ(instance.count, 3);
  EXPECT_EQ(instance.mode, Mode::kB);
  EXPECT_FLOAT_EQ(instance.speed, 2.0f);
  EXPECT_EQ(reader.GetWarnings().size(), 2u);
}

TEST(TypeRegistry, BuiltinsAndSchemaRoundTrip) {
  RegisterBuiltinTypes();
  auto& registry = TypeRegistry::Instance();
  ASSERT_NE(registry.FindComponent("Transform"), nullptr);
  ASSERT_NE(registry.FindComponent("Rigidbody"), nullptr);
  EXPECT_FALSE(registry.FindComponent("Transform")->removable);
  ASSERT_NE(registry.FindAssetType("Material"), nullptr);

  // A schema written for a script reads back with the same fields.
  ComponentInfo script;
  script.name = "TestScript";
  script.is_script = true;
  script.fields = DescribeFields<Everything>();
  script.attach = [](MapScene&, kEntityId, const mtext::PropertyList&) {};
  registry.RegisterComponent(script);
  const auto file = std::filesystem::temp_directory_path() / "mercury_test.mschema";
  ASSERT_TRUE(registry.SaveSchema(file));

  // Pretend to be the editor: drop the compiled script, load the schema.
  script.attach = nullptr;
  registry.RegisterComponent(script);
  registry.ClearSchemaScripts();
  EXPECT_EQ(registry.FindComponent("TestScript"), nullptr);
  ASSERT_TRUE(registry.LoadSchema(file));
  auto loaded = registry.FindComponent("TestScript");
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(loaded->fields.size(), 10u);
  EXPECT_EQ(loaded->fields[9].type, FieldType::kEnum);
  EXPECT_EQ(loaded->fields[8].asset_type, "Material");
  EXPECT_EQ(loaded->fields[2].tooltip, "fast");
  std::filesystem::remove(file);
  registry.ClearSchemaScripts();
}
