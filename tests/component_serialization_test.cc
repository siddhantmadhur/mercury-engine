// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the reflection-based component snapshot, the stable entity GUIDs,
// and the schema's export of non-script component types.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/reflection.h"
#include "scripting/type_registry.h"

using namespace Mercury;

namespace {

// A component a game would register itself: reflected fields, values stored
// in the scene's registry, with the same get/set accessors a built-in has.
struct SnapshotData {
  int count = 0;
  float speed = 0.0f;
  std::string label;
  glm::vec3 position = glm::vec3(0.0f);

  void Reflect(Reflector& r) {
    r.Field("count", count);
    r.Field("speed", speed);
    r.Field("label", label);
    r.Field("position", position);
  }
};

ComponentInfo MakeSnapshotInfo(const char* name, bool runtime_writable) {
  ComponentInfo info;
  info.name = name;
  info.category = "Tests";
  info.description = "Component used by the serialization tests";
  info.from_game = true;
  info.fields = DescribeFields<SnapshotData>();
  for (FieldInfo& field : info.fields) {
    field.runtime_writable = runtime_writable;
  }
  info.has = [](MapScene& scene, kEntityId entity) {
    return scene.Objects().GetProperty<SnapshotData>(entity) != nullptr;
  };
  info.get_field = [](MapScene& scene, kEntityId entity, std::string_view field,
                      mtext::Variant* out) {
    SnapshotData* value = scene.Objects().GetProperty<SnapshotData>(entity);
    if (!value) return false;
    auto reflector = Reflector::Get(field, out);
    value->Reflect(reflector);
    return reflector.Found();
  };
  info.set_field = [](MapScene& scene, kEntityId entity, std::string_view field,
                      const mtext::Variant& in) {
    SnapshotData* value = scene.Objects().GetProperty<SnapshotData>(entity);
    if (!value) return false;
    auto reflector = Reflector::Set(field, in);
    value->Reflect(reflector);
    return reflector.Found();
  };
  return info;
}

void RegisterSnapshotType() {
  TypeRegistry::Instance().RegisterComponent(
      MakeSnapshotInfo("SnapshotTest", true));
}

std::shared_ptr<Project> MakeProject() {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("mercury_serialization_test_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  auto project = Project::Create(dir / "test.mproj", "SerializationTest");
  project->Save();
  return project;
}

SnapshotData MakeData() {
  SnapshotData data;
  data.count = 42;
  data.speed = 2.5f;
  data.label = "hero";
  data.position = glm::vec3(1.0f, 2.0f, 3.0f);
  return data;
}

}  // namespace

TEST(ComponentSerialization, RoundTripsThroughProperties) {
  RegisterSnapshotType();
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();

  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  mtext::PropertyList snapshot;
  ASSERT_TRUE(
      registry.SerializeComponent(scene, entity, "SnapshotTest", snapshot));
  EXPECT_EQ(snapshot.GetInt("count"), 42);
  EXPECT_DOUBLE_EQ(snapshot.GetFloat("speed"), 2.5);
  EXPECT_EQ(snapshot.GetString("label"), "hero");
  ASSERT_NE(snapshot.Get("position"), nullptr);
  EXPECT_EQ(snapshot.Get("position")->Get<glm::vec3>()->x, 1.0f);

  scene.Objects().AddProperty(entity, SnapshotData{});
  ASSERT_TRUE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", snapshot));
  const SnapshotData* restored =
      scene.Objects().GetProperty<SnapshotData>(entity);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->count, 42);
  EXPECT_FLOAT_EQ(restored->speed, 2.5f);
  EXPECT_EQ(restored->label, "hero");
  EXPECT_EQ(restored->position, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(ComponentSerialization, RoundTripsThroughAByteBuffer) {
  RegisterSnapshotType();
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();

  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  std::vector<uint8_t> bytes;
  ASSERT_TRUE(
      registry.SerializeComponent(scene, entity, "SnapshotTest", bytes));
  EXPECT_FALSE(bytes.empty());

  scene.Objects().AddProperty(entity, SnapshotData{});
  ASSERT_TRUE(registry.DeserializeComponent(scene, entity, "SnapshotTest",
                                            std::span<const uint8_t>(bytes)));
  const SnapshotData* restored =
      scene.Objects().GetProperty<SnapshotData>(entity);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->count, 42);
  EXPECT_EQ(restored->label, "hero");
}

TEST(ComponentSerialization, DeltaLeavesUnlistedFieldsAlone) {
  RegisterSnapshotType();
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();

  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  mtext::PropertyList delta;
  delta.Set("count", 7);
  ASSERT_TRUE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", delta));
  const SnapshotData* value = scene.Objects().GetProperty<SnapshotData>(entity);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->count, 7);
  EXPECT_EQ(value->label, "hero");
}

TEST(ComponentSerialization, UnknownTypesAreRejected) {
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();
  const kEntityId entity = scene.Objects().NewObject();

  mtext::PropertyList properties;
  EXPECT_FALSE(
      registry.SerializeComponent(scene, entity, "NoSuchType", properties));

  std::vector<uint8_t> bytes;
  EXPECT_FALSE(registry.SerializeComponent(scene, entity, "NoSuchType", bytes));
  EXPECT_FALSE(registry.DeserializeComponent(scene, entity, "NoSuchType",
                                             std::span<const uint8_t>(bytes)));
}

TEST(ComponentSerialization, EntityGuidsAreStableAndUnique) {
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);

  const kEntityId first = scene.Objects().NewObject();
  const kEntityId second = scene.Objects().NewObject();
  const uint64_t first_guid = scene.Objects().GetGuid(first);
  const uint64_t second_guid = scene.Objects().GetGuid(second);
  EXPECT_NE(first_guid, 0u);
  EXPECT_NE(second_guid, 0u);
  EXPECT_NE(first_guid, second_guid);

  EXPECT_EQ(scene.Objects().FindByGuid(first_guid), first);
  EXPECT_TRUE(scene.Objects().SetGuid(second, 123456));
  EXPECT_EQ(scene.Objects().GetGuid(second), 123456u);
  EXPECT_EQ(scene.Objects().FindByGuid(123456), second);

  // Changing an id drops the old index entry.
  EXPECT_TRUE(scene.Objects().SetGuid(second, 654321));
  EXPECT_TRUE(scene.Objects().FindByGuid(123456) == entt::null);
  EXPECT_EQ(scene.Objects().FindByGuid(654321), second);

  // Destroying through the registry directly (not DestroyObject) also drops
  // the index entry, through the registry's on_destroy signal.
  const kEntityId third = scene.Objects().NewObject();
  const uint64_t third_guid = scene.Objects().GetGuid(third);
  // A live entity's id cannot be taken by another entity.
  EXPECT_FALSE(scene.Objects().SetGuid(third, 654321));
  EXPECT_EQ(scene.Objects().GetGuid(third), third_guid);
  EXPECT_EQ(scene.Objects().FindByGuid(654321), second);
  scene.Objects().GetRegistry().destroy(third);
  EXPECT_TRUE(scene.Objects().FindByGuid(third_guid) == entt::null);

  scene.Objects().DestroyObject(first);
  EXPECT_TRUE(scene.Objects().FindByGuid(first_guid) == entt::null);
  EXPECT_TRUE(scene.Objects().FindByGuid(0) == entt::null);
}

TEST(ComponentSerialization, ClonedNodesGetUniqueGuids) {
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);

  // A prefab or a duplicated node keeps its source uid, which is not unique
  // for each instance; the entity GUID must still be.
  MapNode node;
  node.name = "Clone";
  node.uid = 777777;

  const GameObject first = scene.InstantiateNode(node, {});
  const GameObject second = scene.InstantiateNode(node, {});
  ASSERT_TRUE(first.IsValid());
  ASSERT_TRUE(second.IsValid());
  const uint64_t first_guid = scene.Objects().GetGuid(first.GetEntity());
  const uint64_t second_guid = scene.Objects().GetGuid(second.GetEntity());
  EXPECT_NE(first_guid, 0u);
  EXPECT_NE(first_guid, 777777u);
  EXPECT_NE(first_guid, second_guid);
}

TEST(ComponentSerialization, ASnapshotThatAppliesNothingIsAFailure) {
  RegisterSnapshotType();
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();
  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  mtext::PropertyList empty;
  EXPECT_FALSE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", empty));

  mtext::PropertyList unknown;
  unknown.Set("not_a_field", 1);
  EXPECT_FALSE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", unknown));
}

TEST(ComponentSerialization, RejectsNonFiniteValues) {
  RegisterSnapshotType();
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();
  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  mtext::PropertyList bad;
  bad.Set("speed", std::numeric_limits<double>::infinity());
  bad.Set("position", glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0));
  EXPECT_FALSE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", bad));
  SnapshotData* value = scene.Objects().GetProperty<SnapshotData>(entity);
  ASSERT_NE(value, nullptr);
  EXPECT_FLOAT_EQ(value->speed, 2.5f);
  EXPECT_EQ(value->position, glm::vec3(1.0f, 2.0f, 3.0f));

  // A finite field in the same snapshot still applies.
  mtext::PropertyList mixed;
  mixed.Set("speed", std::numeric_limits<double>::infinity());
  mixed.Set("count", 9);
  EXPECT_TRUE(
      registry.DeserializeComponent(scene, entity, "SnapshotTest", mixed));
  EXPECT_EQ(value->count, 9);
  EXPECT_FLOAT_EQ(value->speed, 2.5f);
}

TEST(ComponentSerialization, ReadOnlyFieldsNeedAnExplicitOption) {
  TypeRegistry::Instance().RegisterComponent(
      MakeSnapshotInfo("ReadOnlySnapshot", false));
  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kEdit);
  auto& registry = TypeRegistry::Instance();
  const kEntityId entity = scene.Objects().NewObject();
  scene.Objects().AddProperty(entity, MakeData());

  mtext::PropertyList properties;
  properties.Set("count", 5);
  EXPECT_FALSE(registry.DeserializeComponent(scene, entity, "ReadOnlySnapshot",
                                             properties));
  SnapshotData* value = scene.Objects().GetProperty<SnapshotData>(entity);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->count, 42);

  ComponentDeserializeOptions trusted;
  trusted.require_runtime_writable = false;
  EXPECT_TRUE(registry.DeserializeComponent(scene, entity, "ReadOnlySnapshot",
                                            properties, trusted));
  EXPECT_EQ(value->count, 5);
}

TEST(ComponentSerialization, SchemaExportsNonScriptComponents) {
  RegisterSnapshotType();
  const auto path =
      std::filesystem::temp_directory_path() /
      ("mercury_schema_" + std::to_string(std::random_device{}()) + ".mschema");
  ASSERT_TRUE(TypeRegistry::Instance().SaveSchema(path));

  std::ifstream in(path);
  ASSERT_TRUE(in.good());
  const std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("name=\"SnapshotTest\""), std::string::npos);
  EXPECT_NE(text.find("count"), std::string::npos);
  std::filesystem::remove(path);
}
