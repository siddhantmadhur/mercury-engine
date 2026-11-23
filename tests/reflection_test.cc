// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scripting/reflection.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "components/components.h"
#include "project/project.h"
#include "properties/render_mesh.h"
#include "scene/map_scene.h"
#include "scripting/type_registry.h"

using namespace Mercury;

namespace {

enum class Mode { kA, kB };

struct WritableFields {
  int count = 1;
  float speed = 2.0f;

  void Reflect(Reflector& r) {
    r.Field("count", count);
    r.Field("speed", speed).Tooltip("run time").RuntimeWritable();
  }
};

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

TEST(Reflection, RuntimeWritableMarksOnlyTheChosenField) {
  auto fields = DescribeFields<WritableFields>();
  ASSERT_EQ(fields.size(), 2u);
  EXPECT_FALSE(fields[0].runtime_writable);
  EXPECT_TRUE(fields[1].runtime_writable);
  EXPECT_EQ(fields[1].tooltip, "run time");

  RegisterBuiltinTypes();
  const auto* sprite = TypeRegistry::Instance().FindComponent("SpriteRenderer");
  ASSERT_NE(sprite, nullptr);
  const auto* mesh = TypeRegistry::Instance().FindComponent("MeshRenderer");
  ASSERT_NE(mesh, nullptr);
  for (const auto* info : {sprite, mesh}) {
    EXPECT_TRUE(info->FindField("color")->runtime_writable);
    EXPECT_TRUE(info->FindField("visible")->runtime_writable);
  }
}

TEST(Reflection, SettingSpriteColorReachesRenderMeshAndBumpsRevision) {
  RegisterBuiltinTypes();
  const auto dir = std::filesystem::temp_directory_path() /
                   ("mercury_renderer_fields_test_" +
                    std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  auto project = Project::Create(dir / "test.mproj", "RendererFieldsTest");
  MapScene scene(project, MapScene::Mode::kEdit);
  const kEntityId entity = scene.Objects().NewObject();
  ASSERT_TRUE(
      scene.AddComponent(scene.GetGameObject(entity), "SpriteRenderer"));

  auto* sprite = scene.Objects().GetProperty<SpriteRenderer>(entity);
  auto* render = scene.Objects().GetProperty<RenderMesh>(entity);
  ASSERT_NE(sprite, nullptr);
  ASSERT_NE(render, nullptr);
  ASSERT_EQ(render->materials.size(), 1u);
  EXPECT_EQ(render->revision, 0u);

  const auto* info = TypeRegistry::Instance().FindComponent("SpriteRenderer");
  ASSERT_NE(info, nullptr);
  const glm::vec4 color(0.25f, 0.5f, 0.75f, 0.4f);
  ASSERT_TRUE(info->set_field(scene, entity, "color", mtext::Color{color}));
  EXPECT_EQ(sprite->color, color);
  EXPECT_EQ(render->materials[0].color, color);  // no material asset: white
  EXPECT_EQ(render->revision, 1u);

  ASSERT_TRUE(info->set_field(scene, entity, "visible", mtext::Variant(false)));
  EXPECT_FALSE(sprite->visible);
  EXPECT_FALSE(render->visible);
  EXPECT_EQ(render->revision, 2u);

  // A field without an after-set hook must not touch the renderer.
  EXPECT_TRUE(info->set_field(scene, entity, "material",
                              mtext::Resource{"res://ignored.masset"}));
  EXPECT_EQ(render->revision, 2u);
  std::filesystem::remove_all(dir);
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

// A script or a file can set an enum by number; a number that is not one of
// the choices must not become the value (it would index tables out of range).
TEST(Reflection, SettingAnEnumByNumberChecksTheRange) {
  Everything instance;
  for (int64_t bad : {int64_t{-1}, int64_t{2}, int64_t{1} << 40}) {
    mtext::Variant value(bad);
    auto setter = Reflector::Set("mode", value);
    instance.Reflect(setter);
    EXPECT_TRUE(setter.Found());
    EXPECT_EQ(instance.mode, Mode::kB) << bad;
    EXPECT_EQ(setter.GetWarnings().size(), 1u) << bad;
  }
  mtext::Variant zero(int64_t{0});
  auto setter = Reflector::Set("mode", zero);
  instance.Reflect(setter);
  EXPECT_EQ(instance.mode, Mode::kA);
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
  const auto file =
      std::filesystem::temp_directory_path() / "mercury_test.mschema";
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

// The schema's [[field]] entries are the single representation of a field:
// save -> load keeps declaration order (the inspector's order) and details.
TEST(TypeRegistry, SchemaFieldsKeepDeclarationOrderAndDetails) {
  RegisterBuiltinTypes();
  auto& registry = TypeRegistry::Instance();
  ComponentInfo script;
  script.name = "OrderScript";
  script.is_script = true;
  script.fields = DescribeFields<Everything>();
  script.attach = nullptr;
  registry.RegisterComponent(script);
  const auto file =
      std::filesystem::temp_directory_path() / "mercury_order.mschema";
  ASSERT_TRUE(registry.SaveSchema(file));
  registry.ClearSchemaScripts();
  ASSERT_TRUE(registry.LoadSchema(file));
  auto loaded = registry.FindComponent("OrderScript");
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(loaded->fields.size(), 10u);
  // Declaration order, not the alphabetical order the TOML reader would
  // give [script.props].
  int index = 0;
  for (const char* name : {"flag", "count", "speed", "label", "v2", "v3",
                           "color", "target", "material", "mode"}) {
    EXPECT_EQ(loaded->fields[index].name, name) << index;
    index++;
  }
  EXPECT_EQ(loaded->fields[0].default_value.ToBool(), true);
  EXPECT_EQ(loaded->fields[2].tooltip, "fast");
  EXPECT_EQ(loaded->fields[8].asset_type, "Material");
  EXPECT_EQ(loaded->fields[9].type, FieldType::kEnum);
  EXPECT_EQ(loaded->fields[9].enum_values,
            (std::vector<std::string>{"a", "b"}));
  std::filesystem::remove(file);
  registry.ClearSchemaScripts();
}

TEST(Lighting, MaterialAssetPbrPropertiesReflectAndSerialize) {
  MaterialAsset mat;
  mat.color = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
  mat.roughness = 0.25f;
  mat.metallic = 0.85f;

  mtext::PropertyList properties;
  auto writer = Reflector::Write(properties);
  mat.Reflect(writer);

  MaterialAsset loaded = ReadFields<MaterialAsset>(properties, "Material");
  EXPECT_EQ(loaded.color, glm::vec4(0.8f, 0.4f, 0.2f, 1.0f));
  EXPECT_FLOAT_EQ(loaded.roughness, 0.25f);
  EXPECT_FLOAT_EQ(loaded.metallic, 0.85f);
}

TEST(Lighting, DirectionalLightShadowPropertiesReflectAndSerialize) {
  DirectionalLight light;
  light.color = glm::vec4(1.0f, 0.95f, 0.9f, 1.0f);
  light.intensity = 1.8f;
  light.ambient = glm::vec4(0.2f, 0.25f, 0.3f, 1.0f);
  light.cast_shadows = true;
  light.shadow_bias = 0.002f;
  light.shadow_normal_bias = 0.004f;
  light.shadow_distance = 75.0f;

  mtext::PropertyList properties;
  auto writer = Reflector::Write(properties);
  light.Reflect(writer);

  DirectionalLight loaded =
      ReadFields<DirectionalLight>(properties, "DirectionalLight");
  EXPECT_EQ(loaded.color, glm::vec4(1.0f, 0.95f, 0.9f, 1.0f));
  EXPECT_FLOAT_EQ(loaded.intensity, 1.8f);
  EXPECT_EQ(loaded.ambient, glm::vec4(0.2f, 0.25f, 0.3f, 1.0f));
  EXPECT_TRUE(loaded.cast_shadows);
  EXPECT_FLOAT_EQ(loaded.shadow_bias, 0.002f);
  EXPECT_FLOAT_EQ(loaded.shadow_normal_bias, 0.004f);
  EXPECT_FLOAT_EQ(loaded.shadow_distance, 75.0f);
}
