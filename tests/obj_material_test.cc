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
#include <fstream>
#include <string>
#include <vector>

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

  // illum 5, Kd 0.01, Ks 0.95, Ns 1000: a metal tinted by Kd + Ks. Ns 1000
  // gives GGX alpha sqrt(2 / 1002) (inside the 0.1 cap), so the perceptual
  // roughness Filament wants is its square root.
  const ModelMaterial* mirror = Find(*model, "leftSphere");
  ASSERT_TRUE(mirror);
  EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
  EXPECT_NEAR(Roughness(*mirror), std::pow(2.0f / 1002.0f, 0.25f), 1e-4f);
  EXPECT_FLOAT_EQ(mirror->transmission, 0.0f);
  EXPECT_NEAR(Linear(mirror->color.r), 0.96f, 1e-3f);

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
  // Glass is polished whatever Ns says.
  EXPECT_NEAR(Roughness(*glass), 0.0f, 1e-5f);

  // The light keeps its emission, and matte walls have no specular (Ks 0).
  const ModelMaterial* light = Find(*model, "light");
  ASSERT_TRUE(light);
  EXPECT_FLOAT_EQ(light->emission.r, 10.0f);
  const ModelMaterial* wall = Find(*model, "leftWall");
  ASSERT_TRUE(wall);
  EXPECT_FLOAT_EQ(wall->metallic, 0.0f);
  EXPECT_FLOAT_EQ(wall->transmission, 0.0f);
  // Ks 0 leaves the default reflectance and a fully matte surface.
  EXPECT_FLOAT_EQ(wall->reflectance, ModelMaterial().reflectance);
  EXPECT_GE(Roughness(*wall), 0.7f);
}

TEST(ObjMaterials, CornellMirrorBox) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Mirror.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  const ModelMaterial* mirror = Find(*model, "tallBox");
  ASSERT_TRUE(mirror);
  EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
  // Ns 1000: alpha sqrt(2 / 1002), perceptual roughness its square root.
  EXPECT_NEAR(Roughness(*mirror), std::pow(2.0f / 1002.0f, 0.25f), 1e-4f);
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

TEST(ObjMaterials, CornellGlossyDielectricsKeepDefaultF0) {
  auto model = LoadBenchmark("models/cornell_box/CornellBox-Glossy.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  // illum 2, Ks 0.9 and 0.8: shiny dielectrics. Ks only modulated the old
  // renderer's glossiness, so both keep its constant 4% dielectric F0;
  // Filament's reflectance 0.5 is F0 = 0.16 * 0.5^2 = 0.04.
  const ModelMaterial* sphere = Find(*model, "sphere");
  ASSERT_TRUE(sphere);
  EXPECT_FLOAT_EQ(sphere->metallic, 0.0f);
  EXPECT_FLOAT_EQ(sphere->transmission, 0.0f);
  EXPECT_FLOAT_EQ(sphere->reflectance, 0.5f);
  const ModelMaterial* box = Find(*model, "shortBox");
  ASSERT_TRUE(box);
  EXPECT_FLOAT_EQ(box->reflectance, 0.5f);
  // Ns 40 gives GGX alpha sqrt(2 / 42); Filament's perceptual roughness is
  // its square root, pow(2 / 42, 0.25) = 0.467.
  EXPECT_NEAR(Roughness(*sphere), std::pow(2.0f / 42.0f, 0.25f), 1e-4f);
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

  // illum 3: the core is metal, tinted by Kd + Ks (saturating at 1).
  for (const char* name : {"InnerMat", "LTELogo"}) {
    const ModelMaterial* metal = Find(*model, name);
    ASSERT_TRUE(metal) << name;
    EXPECT_FLOAT_EQ(metal->metallic, 1.0f) << name;
    EXPECT_FLOAT_EQ(metal->transmission, 0.0f) << name;
  }
  EXPECT_NEAR(Linear(Find(*model, "InnerMat")->color.r), 0.5f, 1e-3f);
  EXPECT_NEAR(Linear(Find(*model, "LTELogo")->color.r), 0.75f, 1e-3f);
  // Ns 200 gives GGX alpha sqrt(2 / 202), perceptual pow(2 / 202, 0.25) =
  // 0.315. Ns 8's alpha 0.447 is capped at a polished alpha 0.1, so its
  // perceptual roughness is sqrt(0.1) = 0.316.
  EXPECT_NEAR(Roughness(*Find(*model, "LTELogo")),
              std::pow(2.0f / 202.0f, 0.25f), 1e-4f);
  EXPECT_NEAR(Roughness(*Find(*model, "InnerMat")), std::sqrt(0.1f), 1e-4f);
}

TEST(ObjMaterials, BlenderIllum4IsNotGlass) {
  auto model = LoadBenchmark("models/breakfast_room/breakfast_room.obj");
  if (!model) GTEST_SKIP() << "missing benchmark fixture";
  // Blender writes illum 4 with Tf 1 1 1 and Ni 1 on every material; none of
  // them refracts.
  const ModelMaterial* ceramic = Find(*model, "breakfast_room:Ceramic");
  if (!ceramic) GTEST_SKIP() << "no Ceramic material";
  EXPECT_FLOAT_EQ(ceramic->transmission, 0.0f);
  for (const ModelMaterial& material : model->Materials()) {
    EXPECT_FLOAT_EQ(material.transmission, 0.0f) << material.name;
  }
}

namespace {

struct SyntheticMaterial {
  const char* name;
  const char* lines;  // the .mtl body after newmtl
};

// Writes one triangle per material and the .mtl that describes them, so the
// MTL mapping is covered without the benchmark project.
std::shared_ptr<Model> LoadSynthetic(
    const std::vector<SyntheticMaterial>& materials, const std::string& stem) {
  UseTestCache();
  const fs::path dir = Mercury::test::CacheDir() / "obj_material_synthetic";
  fs::create_directories(dir);
  {
    std::ofstream mtl(dir / (stem + ".mtl"));
    for (const SyntheticMaterial& m : materials)
      mtl << "newmtl " << m.name << "\n" << m.lines << "\n";
  }
  {
    std::ofstream obj(dir / (stem + ".obj"));
    obj << "mtllib " << stem << ".mtl\n";
    int index = 0;
    for (const SyntheticMaterial& m : materials) {
      const float y = static_cast<float>(index++) * 2.0f;
      obj << "o " << m.name << "\n";
      obj << "v 0 " << y << " 0\nv 1 " << y << " 0\nv 0 " << y + 1 << " 0\n";
      obj << "usemtl " << m.name << "\n";
      obj << "f " << 3 * (index - 1) + 1 << " " << 3 * (index - 1) + 2 << " "
          << 3 * (index - 1) + 3 << "\n";
    }
  }
  ModelImportSettings settings;
  settings.max_texture_size = 64;
  std::string error;
  auto model = Model::Load((dir / (stem + ".obj")).string(), settings, &error);
  EXPECT_TRUE(model) << error;
  return model;
}

}  // namespace

TEST(ObjMaterials, SyntheticMtlMapping) {
  const std::vector<SyntheticMaterial> materials = {
      {"matte", "illum 2\nKd 0.5 0.5 0.5\nKs 0 0 0\nNs 300\n"},
      // Ks 0 with a huge exponent: the old glossiness attenuation still
      // makes it fully rough rather than a sharp 4% mirror.
      {"mattehigh", "illum 2\nKd 0.5 0.5 0.5\nKs 0 0 0\nNs 1000\n"},
      {"plastic", "illum 2\nKd 0.5 0.5 0.5\nKs 0.04 0.04 0.04\nNs 50\n"},
      {"glossy", "illum 2\nKd 0.5 0.5 0.5\nKs 0.9 0.9 0.9\nNs 40\n"},
      {"mirror", "illum 5\nKd 0 0 0\nKs 0.95 0.95 0.95\nNs 1000\n"},
      {"clear", "illum 7\nKd 0.01 0.01 0.01\nTf 0.1 0.1 0.1\nNi 1.5\n"},
      {"amber", "illum 4\nKd 0.8 0.4 0.2\nTf 1 1 1\nNi 1.5\n"},
      {"blender",
       "illum 4\nKd 0.64 0.64 0.64\nTf 1 1 1\nNi 1.00\n"
       "Ks 0.5 0.5 0.5\nNs 96\n"},
      {"bare", "illum 2\nKd 0 0 0\nKs 0.8 0.8 0.8\nNs 100\n"},
      {"satinmetal", "illum 3\nKd 0.2 0.2 0.2\nKs 0.3 0.3 0.3\nNs 8\n"},
      {"brightmetal", "illum 5\nKd 0.8 0.8 0.8\nKs 0.9 0.9 0.9\nNs 1000\n"},
      {"weakmetal", "illum 3\nKd 0.5 0.5 0.5\nKs 0.1 0.1 0.1\nNs 100\n"},
  };
  // The second load comes from the model cache and must agree.
  for (int pass = 0; pass < 2; pass++) {
    SCOPED_TRACE(pass == 0 ? "imported" : "cached");
    auto model = LoadSynthetic(materials, "synthetic_mtl");
    ASSERT_TRUE(model);

    const ModelMaterial* matte = Find(*model, "matte");
    ASSERT_TRUE(matte);
    EXPECT_FLOAT_EQ(matte->reflectance, 0.5f);
    // Ks 0 -> gloss factor 0, so Ns cannot make it shiny.
    EXPECT_FLOAT_EQ(Roughness(*matte), 1.0f);
    EXPECT_FLOAT_EQ(matte->metallic, 0.0f);

    const ModelMaterial* mattehigh = Find(*model, "mattehigh");
    ASSERT_TRUE(mattehigh);
    EXPECT_FLOAT_EQ(Roughness(*mattehigh), 1.0f);
    EXPECT_FLOAT_EQ(mattehigh->metallic, 0.0f);

    const ModelMaterial* plastic = Find(*model, "plastic");
    ASSERT_TRUE(plastic);
    // Ks only modulated glossiness historically; a dielectric keeps its 4%
    // F0 whatever Ks says, i.e. Filament reflectance 0.5.
    EXPECT_FLOAT_EQ(plastic->reflectance, 0.5f);
    // Ns 50 -> alpha sqrt(2 / 52), perceptual pow(2 / 52, 0.25) = 0.443,
    // scaled by the historical Ks gloss factor min(1, 4*0.04) = 0.16:
    // roughness = 1 - (1 - 0.443) * 0.16 = 0.911.
    EXPECT_NEAR(Roughness(*plastic),
                1.0f - (1.0f - std::pow(2.0f / 52.0f, 0.25f)) * 0.16f, 1e-4f);

    const ModelMaterial* glossy = Find(*model, "glossy");
    ASSERT_TRUE(glossy);
    EXPECT_FLOAT_EQ(glossy->reflectance, 0.5f);
    EXPECT_FLOAT_EQ(glossy->metallic, 0.0f);
    // Ks 0.9 -> gloss factor 1, so the roughness is the pure Ns chain:
    // Ns 40 -> perceptual pow(2 / 42, 0.25) = 0.467.
    EXPECT_NEAR(Roughness(*glossy), std::pow(2.0f / 42.0f, 0.25f), 1e-4f);

    const ModelMaterial* mirror = Find(*model, "mirror");
    ASSERT_TRUE(mirror);
    EXPECT_FLOAT_EQ(mirror->metallic, 1.0f);
    // Ns 1000 -> alpha sqrt(2 / 1002), perceptual pow(2 / 1002, 0.25).
    EXPECT_NEAR(Roughness(*mirror), std::pow(2.0f / 1002.0f, 0.25f), 1e-4f);

    const ModelMaterial* clear = Find(*model, "clear");
    ASSERT_TRUE(clear);
    EXPECT_FLOAT_EQ(clear->transmission, 1.0f);
    EXPECT_NEAR(clear->ior, 1.5f, 1e-5f);
    EXPECT_NEAR(clear->color.r, 1.0f, 1e-3f);
    EXPECT_NEAR(clear->color.g, 1.0f, 1e-3f);
    EXPECT_NEAR(clear->color.b, 1.0f, 1e-3f);
    EXPECT_NEAR(clear->transmission_color.r, 1.0f, 1e-3f);
    EXPECT_NEAR(clear->transmission_color.g, 1.0f, 1e-3f);
    EXPECT_NEAR(clear->transmission_color.b, 1.0f, 1e-3f);

    const ModelMaterial* amber = Find(*model, "amber");
    ASSERT_TRUE(amber);
    EXPECT_FLOAT_EQ(amber->transmission, 1.0f);
    EXPECT_NEAR(amber->transmission_color.r, 1.0f, 1e-3f);
    EXPECT_NEAR(amber->transmission_color.g, 0.5f, 1e-3f);
    EXPECT_NEAR(amber->transmission_color.b, 0.25f, 1e-3f);

    // Tf 1 and Ni 1 ask for no refraction whatever illum says.
    const ModelMaterial* blender = Find(*model, "blender");
    ASSERT_TRUE(blender);
    EXPECT_FLOAT_EQ(blender->transmission, 0.0f);
    EXPECT_FLOAT_EQ(blender->metallic, 0.0f);

    const ModelMaterial* bare = Find(*model, "bare");
    ASSERT_TRUE(bare);
    EXPECT_FLOAT_EQ(bare->metallic, 1.0f);
    EXPECT_NEAR(Linear(bare->color.r), 0.8f, 1e-3f);

    // illum 3/5 metals: Kd + Ks, saturated, with GGX alpha capped at a
    // polished 0.1, i.e. perceptual roughness sqrt(0.1) = 0.316.
    const ModelMaterial* satin = Find(*model, "satinmetal");
    ASSERT_TRUE(satin);
    EXPECT_FLOAT_EQ(satin->metallic, 1.0f);
    EXPECT_NEAR(Linear(satin->color.r), 0.5f, 1e-3f);
    // Ns 8 -> alpha sqrt(2 / 10) = 0.447, capped to 0.1.
    EXPECT_NEAR(Roughness(*satin), std::sqrt(0.1f), 1e-4f);
    const ModelMaterial* bright = Find(*model, "brightmetal");
    ASSERT_TRUE(bright);
    EXPECT_NEAR(Linear(bright->color.r), 1.0f, 1e-3f);
    // Ns 1000 -> alpha sqrt(2 / 1002), perceptual pow(2 / 1002, 0.25).
    EXPECT_NEAR(Roughness(*bright), std::pow(2.0f / 1002.0f, 0.25f), 1e-4f);

    // illum 3 with a weak Ks is a dielectric, not a metal, and keeps the
    // historical 4% F0 (Filament reflectance 0.5).
    const ModelMaterial* weak = Find(*model, "weakmetal");
    ASSERT_TRUE(weak);
    EXPECT_FLOAT_EQ(weak->metallic, 0.0f);
    EXPECT_FLOAT_EQ(weak->reflectance, 0.5f);
    // Ks 0.1 -> gloss factor 0.4; Ns 100's perceptual roughness
    // pow(2 / 102, 0.25) = 0.374 is scaled to 1 - (1 - 0.374) * 0.4 = 0.750.
    EXPECT_NEAR(Roughness(*weak),
                1.0f - (1.0f - std::pow(2.0f / 102.0f, 0.25f)) * 0.4f, 1e-4f);
  }
}
