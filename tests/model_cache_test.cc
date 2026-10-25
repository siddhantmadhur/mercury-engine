// Tests for the model cache: a model read back from it must be the one the
// import built, and a change to the import settings must import again.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

#include "properties/model.h"

using namespace Mercury;

namespace {

namespace fs = std::filesystem;

void SetCacheDir(const fs::path& dir);

// Every test in this binary caches the models it imports in the build
// folder (MERCURY_TEST_CACHE_DIR), not in the user's cache.
const bool kTestCacheDir = [] {
  if (!std::getenv("MERCURY_CACHE_DIR")) SetCacheDir(MERCURY_TEST_CACHE_DIR);
  return true;
}();

const fs::path kModel =
    fs::path(MERCURY_SOURCE_DIR) / "examples/model_viewer/models/breakfast_room/breakfast_room.obj";

void SetCacheDir(const fs::path& dir) {
#if defined(_WIN32)
  _putenv_s("MERCURY_CACHE_DIR", dir.string().c_str());
#else
  if (dir.empty()) {
    unsetenv("MERCURY_CACHE_DIR");
  } else {
    setenv("MERCURY_CACHE_DIR", dir.string().c_str(), 1);
  }
#endif
}

// Points the cache at a fresh folder for the test, and back after it.
class ModelCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    previous_ = std::getenv("MERCURY_CACHE_DIR") ? std::getenv("MERCURY_CACHE_DIR") : "";
    dir_ = fs::temp_directory_path() /
           ("mercury_model_cache_test_" + std::to_string(std::random_device()()));
    fs::remove_all(dir_);
    SetCacheDir(dir_);
  }
  void TearDown() override {
    SetCacheDir(previous_);
    fs::remove_all(dir_);
  }
  fs::path previous_;
  fs::path dir_;
};

ModelImportSettings Settings() {
  ModelImportSettings settings;
  settings.max_texture_size = 64;
  return settings;
}

}  // namespace

TEST_F(ModelCacheTest, ReadBackIsTheImportedModel) {
  std::string error;
  auto imported = Model::Load(kModel.string(), Settings(), &error);
  ASSERT_TRUE(imported) << error;
  ASSERT_TRUE(fs::exists(dir_ / "models"));
  auto cached = Model::Load(kModel.string(), Settings(), &error);
  ASSERT_TRUE(cached) << error;

  ASSERT_EQ(imported->Nodes().size(), cached->Nodes().size());
  for (size_t i = 0; i < imported->Nodes().size(); i++) {
    const ModelNode& a = imported->Nodes()[i];
    const ModelNode& b = cached->Nodes()[i];
    EXPECT_EQ(a.name, b.name);
    EXPECT_EQ(a.parent, b.parent);
    EXPECT_EQ(a.children, b.children);
    EXPECT_EQ(a.meshes, b.meshes);
    EXPECT_EQ(a.LocalMatrix(), b.LocalMatrix());
  }
  ASSERT_EQ(imported->Materials().size(), cached->Materials().size());
  for (size_t i = 0; i < imported->Materials().size(); i++) {
    const ModelMaterial& a = imported->Materials()[i];
    const ModelMaterial& b = cached->Materials()[i];
    EXPECT_EQ(a.name, b.name);
    EXPECT_EQ(a.color, b.color);
    EXPECT_EQ(a.smoothness, b.smoothness);
    EXPECT_EQ(a.transmission, b.transmission);
    EXPECT_EQ(a.ior, b.ior);
    EXPECT_EQ(a.transmission_color, b.transmission_color);
    EXPECT_EQ(a.thickness, b.thickness);
    for (auto [x, y] : {std::pair(a.texture, b.texture),
                        std::pair(a.normal_texture, b.normal_texture),
                        std::pair(a.emission_texture, b.emission_texture)}) {
      ASSERT_EQ(bool(x), bool(y)) << a.name;
      if (!x) continue;
      EXPECT_EQ(x->width, y->width);
      EXPECT_EQ(x->srgb, y->srgb);
      EXPECT_EQ(x->pixels, y->pixels);
    }
  }
  ASSERT_EQ(imported->Meshes().size(), cached->Meshes().size());
  for (size_t i = 0; i < imported->Meshes().size(); i++) {
    const MeshGeometry& a = *imported->Meshes()[i].geometry;
    const MeshGeometry& b = *cached->Meshes()[i].geometry;
    EXPECT_EQ(imported->Meshes()[i].material, cached->Meshes()[i].material);
    ASSERT_EQ(a.vertices.size(), b.vertices.size());
    EXPECT_EQ(std::memcmp(a.vertices.data(), b.vertices.data(),
                          a.vertices.size() * sizeof(ModelVertex)),
              0);
    EXPECT_EQ(a.indices, b.indices);
    EXPECT_EQ(a.clusters.size(), b.clusters.size());
    EXPECT_EQ(a.bounds_min, b.bounds_min);
    EXPECT_EQ(a.bounds_max, b.bounds_max);
    EXPECT_NE(a.id, b.id);  // separate GPU uploads
  }
  EXPECT_EQ(imported->BoundsMin(), cached->BoundsMin());
  EXPECT_EQ(imported->BoundsMax(), cached->BoundsMax());
}

TEST_F(ModelCacheTest, OtherSettingsImportAgain) {
  std::string error;
  auto first = Model::Load(kModel.string(), Settings(), &error);
  ASSERT_TRUE(first) << error;
  ModelImportSettings bigger = Settings();
  bigger.scale = 2.0f;
  auto scaled = Model::Load(kModel.string(), bigger, &error);
  ASSERT_TRUE(scaled) << error;
  EXPECT_FLOAT_EQ(scaled->BoundsMax().y, first->BoundsMax().y * 2.0f);
}

// OBJ glass (illum 7 with a transmission filter) imports as transmissive,
// with its index of refraction and tint, and the cache keeps them.
TEST_F(ModelCacheTest, ObjGlassIsTransmissive) {
  const fs::path model = fs::path(MERCURY_SOURCE_DIR) /
                         "examples/model_viewer/models/cornell_box/CornellBox-Sphere.obj";
  for (int pass = 0; pass < 2; pass++) {  // imported, then from the cache
    std::string error;
    auto loaded = Model::Load(model.string(), Settings(), &error);
    ASSERT_TRUE(loaded) << error;
    const ModelMaterial* glass = nullptr;
    const ModelMaterial* mirror = nullptr;
    for (const ModelMaterial& material : loaded->Materials()) {
      if (material.name == "rightSphere") glass = &material;
      if (material.name == "leftSphere") mirror = &material;
    }
    ASSERT_TRUE(glass && mirror);
    EXPECT_FLOAT_EQ(glass->transmission, 1.0f);
    EXPECT_FLOAT_EQ(glass->ior, 2.5f);
    EXPECT_NEAR(glass->transmission_color.r, 0.1f, 1e-5f);
    EXPECT_FALSE(glass->transparent);
    EXPECT_FLOAT_EQ(mirror->transmission, 0.0f);
    EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
  }
}
