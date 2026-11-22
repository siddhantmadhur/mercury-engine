// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests for the introspection API: the schema JSON, did-you-mean
// suggestions, project validation (typo fixtures, missing res:// files,
// the roll_a_ball example) and asset inspection.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "introspection/introspection.h"
#include "project/project.h"
#include "scripting/type_registry.h"
#include "test_paths.h"

namespace fs = std::filesystem;

namespace {

// A throwaway project: a .mproj in a per-test temp folder, so fixtures can
// be written beside it and found through res:// paths.
std::shared_ptr<Mercury::Project> MakeProject(const fs::path& dir) {
  // Start from a clean folder: the temp dir survives between runs, and a
  // stale fixture would make the tests order-dependent.
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  auto project = Mercury::Project::Create(dir / "test.mproj", "Test");
  return project;
}

void WriteFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << text;
}

bool HasDiagnostic(const Mercury::introspection::ValidationReport& report,
                   const std::string& needle) {
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.message.find(needle) != std::string::npos ||
        diagnostic.suggestion.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// A minimal 16-bit PCM WAV, so the audio inspector has something to read
// without a binary fixture.
std::string MakeWav(int frames, int channels, int sample_rate) {
  const int bytes = frames * channels * 2;
  std::string data;
  data += "RIFF";
  const uint32_t riff_size = 36 + bytes;
  data.append(reinterpret_cast<const char*>(&riff_size), 4);
  data += "WAVEfmt ";
  const uint32_t chunk = 16;
  data.append(reinterpret_cast<const char*>(&chunk), 4);
  const uint16_t pcm = 1, block = channels * 2;
  const uint32_t byte_rate = sample_rate * block;
  data.append(reinterpret_cast<const char*>(&pcm), 2);
  const uint16_t mono = channels;
  data.append(reinterpret_cast<const char*>(&mono), 2);
  data.append(reinterpret_cast<const char*>(&sample_rate), 4);
  data.append(reinterpret_cast<const char*>(&byte_rate), 4);
  data.append(reinterpret_cast<const char*>(&block), 2);
  const uint16_t bits = 16;
  data.append(reinterpret_cast<const char*>(&bits), 2);
  data += "data";
  data.append(reinterpret_cast<const char*>(&bytes), 4);
  for (int i = 0; i < frames; i++) {
    for (int c = 0; c < channels; c++) {
      const int16_t sample = static_cast<int16_t>(i * 300);
      data.append(reinterpret_cast<const char*>(&sample), 2);
    }
  }
  return data;
}

}  // namespace

TEST(SuggestTest, FindsNearbyComponentName) {
  const std::vector<std::string> candidates = {"Transform", "Rigidbody",
                                               "BoxCollider", "SpriteRenderer"};
  const auto suggestions =
      Mercury::introspection::Suggest("Rigidbdy", candidates);
  ASSERT_FALSE(suggestions.empty());
  EXPECT_EQ(suggestions.front(), "Rigidbody");
}

TEST(SuggestTest, FindsNearbyFieldName) {
  const auto suggestions = Mercury::introspection::Suggest(
      "masss", {"mass", "speed", "friction", "restitution"});
  ASSERT_FALSE(suggestions.empty());
  EXPECT_EQ(suggestions.front(), "mass");
}

TEST(SuggestTest, IsCaseInsensitive) {
  const auto suggestions =
      Mercury::introspection::Suggest("RIGIDBODY", {"Rigidbody"});
  ASSERT_FALSE(suggestions.empty());
  EXPECT_EQ(suggestions.front(), "Rigidbody");
}

TEST(SuggestTest, HandlesTransposition) {
  // "hsot" -> "shot" is one adjacent transposition.
  const auto suggestions = Mercury::introspection::Suggest("hsot", {"shot"});
  ASSERT_FALSE(suggestions.empty());
  EXPECT_EQ(suggestions.front(), "shot");
}

TEST(SuggestTest, ReturnsNothingForUnrelatedWords) {
  EXPECT_TRUE(Mercury::introspection::Suggest("totallydifferent",
                                              {"Rigidbody", "Transform"})
                  .empty());
  EXPECT_TRUE(Mercury::introspection::Suggest("x", {"speed"}).empty());
  EXPECT_TRUE(Mercury::introspection::Suggest("", {"speed"}).empty());
}

TEST(SuggestTest, RespectsMax) {
  const auto suggestions = Mercury::introspection::Suggest(
      "mas", {"mass", "masss", "mastic", "maxed", "unrelated"}, 2);
  EXPECT_LE(suggestions.size(), 2);
}

TEST(EngineSchemaTest, ListsComponentsAndAssetTypes) {
  const std::string json = Mercury::introspection::EngineSchemaJson();
  const auto document = nlohmann::json::parse(json);
  ASSERT_TRUE(document.contains("components"));
  ASSERT_TRUE(document.contains("asset_types"));

  const auto& components = document["components"];
  const auto rigidbody = std::find_if(
      components.begin(), components.end(),
      [](const nlohmann::json& c) { return c["name"] == "Rigidbody"; });
  ASSERT_NE(rigidbody, components.end()) << json;
  const auto& fields = (*rigidbody)["fields"];
  const auto mass =
      std::find_if(fields.begin(), fields.end(),
                   [](const nlohmann::json& f) { return f["name"] == "mass"; });
  ASSERT_NE(mass, fields.end());
  EXPECT_EQ((*mass)["type"], "float");
  EXPECT_TRUE((*mass)["default"].is_number());

  const auto motion_type = std::find_if(
      fields.begin(), fields.end(),
      [](const nlohmann::json& f) { return f["name"] == "motion_type"; });
  ASSERT_NE(motion_type, fields.end());
  EXPECT_EQ((*motion_type)["type"], "enum");
  EXPECT_EQ((*motion_type)["enum_values"],
            (std::vector<std::string>{"static", "kinematic", "dynamic"}));

  const auto& asset_types = document["asset_types"];
  const auto material = std::find_if(
      asset_types.begin(), asset_types.end(),
      [](const nlohmann::json& a) { return a["name"] == "Material"; });
  ASSERT_NE(material, asset_types.end()) << json;
  EXPECT_GT((*material)["fields"].size(), 0);
}

class ValidateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    project_ = MakeProject(fs::path(testing::TempDir()) /
                           "mercury_introspection_validate");
    WriteFile(project_->GetRoot() / "maps" / "main.mmap", R"([mmap]
format = 2

[[ext_resource]]
id = "1"
path = "res://assets/tex.png"
type = "Texture"

[[component]]
node = "."
type = "Transform"

[[component]]
node = "."
type = "Rigidbdy"

[[component]]
node = "."
type = "Rigidbody"
[component.props]
masss = 2.0
motion_type = "kinemtaic"

[[component]]
node = "."
type = "MeshRenderer"
[component.props]
material = { ext = "1" }
model = { res = "res://assets/missing.png" }

[[node]]
name = "Root"
)");
    WriteFile(project_->GetRoot() / "assets" / "tex.png", "not really a png");
  }

  std::shared_ptr<Mercury::Project> project_;
};

TEST_F(ValidateTest, TypoFixturesGetSuggestions) {
  const auto report = Mercury::introspection::ValidateProject(*project_);
  EXPECT_TRUE(HasDiagnostic(report, "Rigidbody")) << report.ToText();
  EXPECT_TRUE(HasDiagnostic(report, "mass")) << report.ToText();
  EXPECT_TRUE(HasDiagnostic(report, "kinematic")) << report.ToText();
  // Unknown component types may be unloaded scripts, so they are warnings;
  // an unknown field or enum value on a registered component is a typo the
  // loader silently drops, so it is an error.
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.message.find("Rigidbdy") != std::string::npos) {
      EXPECT_EQ(diagnostic.severity, "warning");
      EXPECT_EQ(diagnostic.file, "res://maps/main.mmap");
      EXPECT_GT(diagnostic.line, 0);
    }
    if (diagnostic.message.find("masss") != std::string::npos ||
        diagnostic.message.find("kinemtaic") != std::string::npos) {
      EXPECT_EQ(diagnostic.severity, "error");
      EXPECT_EQ(diagnostic.file, "res://maps/main.mmap");
      EXPECT_GT(diagnostic.line, 0);
    }
  }
}

TEST_F(ValidateTest, MissingResReferenceIsAnError) {
  const auto report = Mercury::introspection::ValidateProject(*project_);
  EXPECT_TRUE(report.HasErrors());
  bool found = false;
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.severity != "error") continue;
    // The [ext_resource] entry and the direct res value are both reported.
    if (diagnostic.message.find("res://assets/missing.png") !=
        std::string::npos) {
      found = true;
      EXPECT_EQ(diagnostic.file, "res://maps/main.mmap");
      EXPECT_GT(diagnostic.line, 0);
    }
  }
  EXPECT_TRUE(found) << report.ToText();
}

TEST_F(ValidateTest, ReportJsonAndTextCarryTheFindings) {
  const auto report = Mercury::introspection::ValidateProject(*project_);
  const auto json = nlohmann::json::parse(report.ToJson());
  EXPECT_EQ(json["has_errors"], report.HasErrors());
  EXPECT_EQ(json["diagnostics"].size(), report.diagnostics.size());
  const std::string text = report.ToText();
  EXPECT_NE(text.find("res://maps/main.mmap"), std::string::npos);
  EXPECT_NE(text.find("Did you mean"), std::string::npos);
}

TEST(ValidateCleanTest, AWellFormedProjectHasNoDiagnostics) {
  auto project =
      MakeProject(fs::path(testing::TempDir()) / "mercury_introspection_clean");
  WriteFile(project->GetRoot() / "maps" / "main.mmap", R"([mmap]
format = 2

[[component]]
node = "."
type = "Rigidbody"
[component.props]
mass = 2.0
motion_type = "dynamic"

[[node]]
name = "Root"
)");
  WriteFile(project->GetRoot() / "assets" / "thing.masset", R"([masset]
format = 2
type = "PhysicsMaterial"
[masset.props]
friction = 0.5
)");
  const auto report = Mercury::introspection::ValidateProject(*project);
  EXPECT_FALSE(report.HasErrors()) << report.ToText();
  EXPECT_TRUE(report.diagnostics.empty()) << report.ToText();
}

TEST(ValidateExampleTest, RollABallHasNoErrors) {
  const auto project =
      Mercury::Project::Load(fs::path(MERCURY_SOURCE_DIR) / "examples" /
                             "roll_a_ball" / "roll_a_ball.mproj");
  ASSERT_TRUE(project);
  const auto report = Mercury::introspection::ValidateProject(*project);
  EXPECT_FALSE(report.HasErrors()) << report.ToText();
  // Script components (GameManager, PlayerController, ...) are known only
  // through the project's schema, which a fresh checkout has not written
  // yet: they show up as warnings until the editor plays the project.
  EXPECT_TRUE(HasDiagnostic(report, "is not registered")) << report.ToText();
}

// ValidateProject may load a project's scripts.mschema — but only into a
// registry that has no script types yet. An editor's or game's registry is
// theirs; loading a schema would even drop their schema-loaded entries
// (LoadSchema starts with ClearSchemaScripts).
TEST(ValidateSideEffectTest, LeavesARegistryWithScriptsAlone) {
  auto project = MakeProject(fs::path(testing::TempDir()) /
                             "mercury_introspection_sideeffect");
  // A project schema declaring a script component the test registry does
  // not have.
  WriteFile(project->GetRoot() / ".mercury" / "scripts.mschema", R"([mschema]
format = 2

[[script]]
name = "SchemaScript"

[[field]]
script = "SchemaScript"
name = "speed"
[field.props]
default = 60.0
)");
  WriteFile(project->GetRoot() / "maps" / "main.mmap", R"([mmap]
format = 2

[[component]]
node = "."
type = "SchemaScript"

[[node]]
name = "Root"
)");
  auto& registry = Mercury::TypeRegistry::Instance();
  Mercury::ComponentInfo fake;
  fake.name = "IntrospectionFakeScript";
  fake.description = "registered before ValidateProject";
  fake.is_script = true;
  registry.RegisterComponent(fake);

  const auto report = Mercury::introspection::ValidateProject(*project);

  // The registry is unchanged: the fake script survived, and the schema's
  // component was not added behind the caller's back.
  const Mercury::ComponentInfo* kept =
      registry.FindComponent("IntrospectionFakeScript");
  ASSERT_NE(kept, nullptr);
  EXPECT_EQ(kept->description, "registered before ValidateProject");
  EXPECT_EQ(registry.FindComponent("SchemaScript"), nullptr);
  // The schema's component still reports as unknown, with no suggestion.
  EXPECT_TRUE(HasDiagnostic(report, "SchemaScript")) << report.ToText();
}

class InspectAssetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    project_ = MakeProject(fs::path(testing::TempDir()) /
                           "mercury_introspection_inspect");
  }

  std::shared_ptr<Mercury::Project> project_;
};

TEST_F(InspectAssetTest, ModelBoundsNodesAndClips) {
  WriteFile(project_->GetRoot() / "models" / "triangle.obj",
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  std::string json, error;
  ASSERT_TRUE(Mercury::introspection::InspectAsset(
      *project_, "res://models/triangle.obj", &json, &error))
      << error;
  const auto document = nlohmann::json::parse(json);
  EXPECT_EQ(document["type"], "Model");
  EXPECT_TRUE(document.contains("note"));
  EXPECT_EQ(document["bounds"]["min"], (std::vector<double>{0.0, 0.0, 0.0}));
  EXPECT_EQ(document["bounds"]["max"], (std::vector<double>{1.0, 1.0, 0.0}));
  ASSERT_GT(document["nodes"].size(), 0);
  EXPECT_FALSE(document["nodes"][0]["name"].get<std::string>().empty());
}

TEST_F(InspectAssetTest, TextureDimensionsAndFormat) {
  const fs::path texture =
      fs::path(MERCURY_SOURCE_DIR) / "assets" / "button_square_flat.png";
  ASSERT_TRUE(fs::exists(texture)) << texture;
  fs::create_directories(project_->GetRoot() / "assets");
  fs::copy(texture, project_->GetRoot() / "assets" / "button.png");
  std::string json, error;
  ASSERT_TRUE(Mercury::introspection::InspectAsset(
      *project_, "res://assets/button.png", &json, &error))
      << error;
  const auto document = nlohmann::json::parse(json);
  EXPECT_EQ(document["type"], "Texture");
  EXPECT_GT(document["width"].get<int>(), 0);
  EXPECT_GT(document["height"].get<int>(), 0);
  EXPECT_EQ(document["format"], "RGBA8");
}

TEST_F(InspectAssetTest, AudioClipDurationChannelsAndRate) {
  WriteFile(project_->GetRoot() / "audio" / "blip.wav",
            MakeWav(48000, 2, 48000));  // exactly one second
  std::string json, error;
  ASSERT_TRUE(Mercury::introspection::InspectAsset(
      *project_, "res://audio/blip.wav", &json, &error))
      << error;
  const auto document = nlohmann::json::parse(json);
  EXPECT_EQ(document["type"], "AudioClip");
  EXPECT_EQ(document["channels"], 2);
  EXPECT_EQ(document["sample_rate"], 48000);
  EXPECT_NEAR(document["duration"].get<double>(), 1.0, 0.01);
}

TEST_F(InspectAssetTest, DataAssetTypeAndFields) {
  WriteFile(project_->GetRoot() / "assets" / "mat.masset", R"([masset]
format = 2
type = "Material"
[masset.props]
color = { color = [0.95, 0.95, 0.95, 1.0] }
)");
  std::string json, error;
  ASSERT_TRUE(Mercury::introspection::InspectAsset(
      *project_, "res://assets/mat.masset", &json, &error))
      << error;
  const auto document = nlohmann::json::parse(json);
  EXPECT_EQ(document["type"], "Material");
  const auto& fields = document["fields"];
  const auto color = std::find_if(
      fields.begin(), fields.end(),
      [](const nlohmann::json& f) { return f["name"] == "color"; });
  ASSERT_NE(color, fields.end());
  EXPECT_TRUE((*color)["set"]);
  const auto roughness = std::find_if(
      fields.begin(), fields.end(),
      [](const nlohmann::json& f) { return f["name"] == "roughness"; });
  ASSERT_NE(roughness, fields.end());
  EXPECT_FALSE((*roughness)["set"]);
}

TEST_F(InspectAssetTest, ReportsErrorsForMissingAndBadPaths) {
  std::string json, error;
  EXPECT_FALSE(Mercury::introspection::InspectAsset(
      *project_, "res://assets/nothing.png", &json, &error));
  EXPECT_NE(error.find("not found"), std::string::npos);
  EXPECT_FALSE(Mercury::introspection::InspectAsset(*project_, "../outside.png",
                                                    &json, &error));
  EXPECT_NE(error.find("res://"), std::string::npos);
}
