// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The MTL -> Filament material mapping, on the benchmark project's Cornell
// box and Mori knob: the importer reads the illumination model, Kd, Ks, Ns,
// Tf and Ni and must come out with a mirror, clear glass, amber glass and
// metal where the files describe them. The models are skipped when the
// benchmark project is not beside this repository (see MERCURY_BENCHMARK_DIR).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "properties/model.h"
#include "test_paths.h"

using namespace Mercury;

namespace {

namespace fs = std::filesystem;

void UseTestCache() {
  if (!std::getenv("MERCURY_CACHE_DIR")) {
#if defined(_WIN32)
    _putenv_s("MERCURY_CACHE_DIR", Mercury::test::CacheDir().string().c_str());
#else
    setenv("MERCURY_CACHE_DIR", Mercury::test::CacheDir().string().c_str(), 1);
#endif
  }
}

// Imports a benchmark model with no .mimport overrides, so what is checked is
// the importer's own reading of the .mtl.
std::shared_ptr<Model> LoadBenchmark(const std::string& relative) {
  UseTestCache();
  const fs::path path = Mercury::test::BenchmarkDir() / relative;
  if (!fs::exists(path)) return nullptr;
  ModelImportSettings settings;
  settings.max_texture_size = 64;
  std::string error;
  auto model = Model::Load(path.string(), settings, &error);
  EXPECT_TRUE(model) << path << ": " << error;
  return model;
}

const ModelMaterial* Find(const Model& model, const std::string& name) {
  for (const ModelMaterial& material : model.Materials()) {
    if (material.name == name) return &material;
  }
  ADD_FAILURE() << "no material " << name;
  return nullptr;
}

// Colors are stored sRGB, as Unity has them; the .mtl's numbers are linear.
float Linear(float srgb) {
  return srgb <= 0.04045f ? srgb / 12.92f
                          : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float Roughness(const ModelMaterial& material) {
  return 1.0f - material.smoothness;
}

}  // namespace

TEST(ObjMaterials, CornellSphereMirrorAndGlass) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Sphere.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";

  // illum 5, Ks 0.95, Ns 1024: a metal tinted by Ks, with Ns 1024's 0.044.
  const ModelMaterial* mirror = Find(*model, "leftSphere");
  ASSERT_TRUE(mirror);
  EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
  EXPECT_LT(Roughness(*mirror), 0.06f);
  EXPECT_FLOAT_EQ(mirror->transmission, 0.0f);
  EXPECT_NEAR(Linear(mirror->color.r), 0.95f, 1e-3f);

  // illum 7, Kd 0.01, Tf 0.1, Ni 2.5: clear glass. White base color (Kd 0.01
  // would turn everything black) and a white tint (Tf 0.1 is "mostly clear").
  const ModelMaterial* glass = Find(*model, "rightSphere");
  ASSERT_TRUE(glass);
  EXPECT_FLOAT_EQ(glass->transmission, 1.0f);
  EXPECT_NEAR(glass->color.r, 1.0f, 1e-3f);
  EXPECT_NEAR(glass->color.g, 1.0f, 1e-3f);
  EXPECT_NEAR(glass->color.b, 1.0f, 1e-3f);
  EXPECT_NEAR(glass->ior, 2.5f, 1e-5f);
  EXPECT_NEAR(glass->transmission_color.r, 1.0f, 1e-3f);
  EXPECT_NEAR(glass->transmission_color.g, 1.0f, 1e-3f);
  EXPECT_NEAR(glass->transmission_color.b, 1.0f, 1e-3f);
  EXPECT_FLOAT_EQ(glass->metallic, 0.0f);
  EXPECT_LT(Roughness(*glass), 0.11f);

  // The light keeps its emission, and matte walls have no specular (Ks 0).
  const ModelMaterial* light = Find(*model, "light");
  ASSERT_TRUE(light);
  EXPECT_FLOAT_EQ(light->emission.r, 10.0f);
  const ModelMaterial* wall = Find(*model, "leftWall");
  ASSERT_TRUE(wall);
  EXPECT_FLOAT_EQ(wall->metallic, 0.0f);
  EXPECT_FLOAT_EQ(wall->transmission, 0.0f);
  EXPECT_FLOAT_EQ(wall->reflectance, 0.0f);
}

TEST(ObjMaterials, CornellMirrorBox) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Mirror.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  const ModelMaterial* mirror = Find(*model, "tallBox");
  ASSERT_TRUE(mirror);
  EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
  EXPECT_LT(Roughness(*mirror), 0.06f);
}

TEST(ObjMaterials, CornellWaterIsGlassWithItsIor) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Water.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  const ModelMaterial* water = Find(*model, "water");
  ASSERT_TRUE(water);
  EXPECT_FLOAT_EQ(water->transmission, 1.0f);
  EXPECT_NEAR(water->ior, 1.33f, 1e-5f);
  EXPECT_NEAR(water->color.r, 1.0f, 1e-3f);
  EXPECT_NEAR(water->transmission_color.r, 1.0f, 1e-3f);
}

TEST(ObjMaterials, CornellGlossyDielectricsReflectMore) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Glossy.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  const ModelMaterial kDefault;
  // illum 2, Ks 0.9 and 0.8: still dielectric, but shiny. Ks maps straight
  // onto Filament's reflectance (F0 = 0.16 * r^2).
  const ModelMaterial* sphere = Find(*model, "sphere");
  ASSERT_TRUE(sphere);
  EXPECT_FLOAT_EQ(sphere->metallic, 0.0f);
  EXPECT_FLOAT_EQ(sphere->transmission, 0.0f);
  EXPECT_GT(sphere->reflectance, kDefault.reflectance);
  EXPECT_NEAR(sphere->reflectance, 0.9f, 1e-5f);
  const ModelMaterial* box = Find(*model, "shortBox");
  ASSERT_TRUE(box);
  EXPECT_GT(box->reflectance, kDefault.reflectance);
  // Ns 40 is a tight highlight, roughness sqrt(2 / 42).
  EXPECT_NEAR(Roughness(*sphere), 0.218f, 1e-3f);
}

TEST(ObjMaterials, MoriKnobAmberGlassOverMetal) {
  auto model = LoadBenchmark("models/mori_knob/testObj.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";

  // illum 4, Kd 0.1 0.04 0.01, Ni 1.01: amber glass. The tint is Kd's
  // chromaticity (brightest channel 1), not its near-black value.
  const ModelMaterial* shell = Find(*model, "OuterMat");
  ASSERT_TRUE(shell);
  EXPECT_FLOAT_EQ(shell->transmission, 1.0f);
  EXPECT_NEAR(shell->color.r, 1.0f, 1e-3f);
  EXPECT_NEAR(shell->color.g, 1.0f, 1e-3f);
  EXPECT_NEAR(shell->color.b, 1.0f, 1e-3f);
  const glm::vec3 tint = shell->transmission_color;
  EXPECT_GT(tint.r, tint.g);
  EXPECT_GT(tint.g, tint.b);
  EXPECT_NEAR(tint.r, 1.0f, 1e-3f);
  EXPECT_NEAR(tint.g, 0.4f, 1e-3f);
  EXPECT_NEAR(tint.b, 0.1f, 1e-3f);
  EXPECT_NEAR(shell->ior, 1.01f, 1e-5f);
  EXPECT_FLOAT_EQ(shell->metallic, 0.0f);

  // illum 3: the core is metal, tinted by the larger of Ks and Kd.
  for (const char* name : {"InnerMat", "LTELogo"}) {
    const ModelMaterial* metal = Find(*model, name);
    ASSERT_TRUE(metal) << name;
    EXPECT_FLOAT_EQ(metal->metallic, 1.0f) << name;
    EXPECT_FLOAT_EQ(metal->transmission, 0.0f) << name;
  }
  EXPECT_NEAR(Linear(Find(*model, "InnerMat")->color.r), 0.3f, 1e-3f);
  EXPECT_NEAR(Linear(Find(*model, "LTELogo")->color.r), 0.5f, 1e-3f);
  // Ns 200 is polished, Ns 8 is satin.
  EXPECT_NEAR(Roughness(*Find(*model, "LTELogo")), 0.0995f, 1e-3f);
  EXPECT_NEAR(Roughness(*Find(*model, "InnerMat")), 0.447f, 1e-3f);
}
