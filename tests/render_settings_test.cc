// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "project/project.h"
#include "scene/map_scene.h"

using namespace Mercury;

namespace {

// Writes a project whose [render] section is `render` and reads back the
// settings a MapScene reports for it.
RenderSettings SettingsFor(const std::string& render) {
  const auto dir =
      std::filesystem::temp_directory_path() / "mercury_render_settings_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  {
    std::ofstream(dir / "test.mproj")
        << "[project]\nname = \"Test\"\n"
           "main_map = \"res://maps/a.mmap\"\n"
        << (render.empty() ? "" : "[render]\n" + render);
  }
  std::string error;
  auto project = Project::Load(dir / "test.mproj", &error);
  EXPECT_TRUE(project) << error;
  RenderSettings settings;
  if (project) {
    MapScene scene(project, MapScene::Mode::kPlay);
    settings = scene.GetRenderSettings();
  }
  std::filesystem::remove_all(dir);
  return settings;
}

TEST(RenderSettings, DefaultsToTaaWithoutFsr) {
  const RenderSettings s = SettingsFor("");
  EXPECT_EQ(s.anti_aliasing, RenderSettings::AntiAliasing::kTaa);
  EXPECT_FALSE(s.fsr);
  EXPECT_EQ(s.fsr_quality, RenderSettings::FsrQuality::kUltra);
  EXPECT_FLOAT_EQ(s.render_scale, 0.75f);
  EXPECT_FLOAT_EQ(s.fsr_sharpness, 0.9f);
}

TEST(RenderSettings, ParsesEachAntiAliasingMode) {
  EXPECT_EQ(SettingsFor("anti_aliasing = \"none\"\n").anti_aliasing,
            RenderSettings::AntiAliasing::kNone);
  EXPECT_EQ(SettingsFor("anti_aliasing = \"fxaa\"\n").anti_aliasing,
            RenderSettings::AntiAliasing::kFxaa);
  EXPECT_EQ(SettingsFor("anti_aliasing = \"taa\"\n").anti_aliasing,
            RenderSettings::AntiAliasing::kTaa);
  // An unknown value falls back to the default.
  EXPECT_EQ(SettingsFor("anti_aliasing = \"msaa\"\n").anti_aliasing,
            RenderSettings::AntiAliasing::kTaa);
}

TEST(RenderSettings, LegacyFxaaBoolIsAFallback) {
  EXPECT_EQ(SettingsFor("fxaa = true\n").anti_aliasing,
            RenderSettings::AntiAliasing::kFxaa);
  EXPECT_EQ(SettingsFor("fxaa = false\n").anti_aliasing,
            RenderSettings::AntiAliasing::kNone);
  // anti_aliasing wins when both are present.
  EXPECT_EQ(
      SettingsFor("fxaa = false\nanti_aliasing = \"taa\"\n").anti_aliasing,
      RenderSettings::AntiAliasing::kTaa);
}

TEST(RenderSettings, ParsesFsrKeys) {
  const RenderSettings s = SettingsFor(
      "fsr = true\nfsr_quality = \"high\"\nrender_scale = 0.6\n"
      "fsr_sharpness = 0.25\n");
  EXPECT_TRUE(s.fsr);
  EXPECT_EQ(s.fsr_quality, RenderSettings::FsrQuality::kHigh);
  EXPECT_FLOAT_EQ(s.render_scale, 0.6f);
  EXPECT_FLOAT_EQ(s.fsr_sharpness, 0.25f);
  EXPECT_EQ(SettingsFor("fsr_quality = \"ultra\"\n").fsr_quality,
            RenderSettings::FsrQuality::kUltra);
}

TEST(RenderSettings, ClampsScaleAndSharpness) {
  EXPECT_FLOAT_EQ(SettingsFor("render_scale = 0.1\n").render_scale, 0.5f);
  EXPECT_FLOAT_EQ(SettingsFor("render_scale = 4\n").render_scale, 1.0f);
  EXPECT_FLOAT_EQ(SettingsFor("fsr_sharpness = -2\n").fsr_sharpness, 0.0f);
  EXPECT_FLOAT_EQ(SettingsFor("fsr_sharpness = 3\n").fsr_sharpness, 1.0f);
}

TEST(RenderSettings, ProjectAccessorsRoundTrip) {
  const auto dir =
      std::filesystem::temp_directory_path() / "mercury_render_roundtrip_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "test.mproj") << "[project]\nname = \"Test\"\n";
  std::string error;
  auto project = Project::Load(dir / "test.mproj", &error);
  ASSERT_TRUE(project) << error;
  project->SetRenderAntiAliasing("fxaa");
  project->SetRenderFsr(true);
  project->SetRenderFsrQuality("high");
  project->SetRenderScale(0.5f);
  project->SetRenderFsrSharpness(0.4f);
  ASSERT_TRUE(project->Save());

  auto reloaded = Project::Load(dir / "test.mproj", &error);
  ASSERT_TRUE(reloaded) << error;
  EXPECT_EQ(reloaded->GetRenderAntiAliasing(), "fxaa");
  EXPECT_TRUE(reloaded->GetRenderFsr());
  EXPECT_EQ(reloaded->GetRenderFsrQuality(), "high");
  EXPECT_FLOAT_EQ(reloaded->GetRenderScale(), 0.5f);
  EXPECT_FLOAT_EQ(reloaded->GetRenderFsrSharpness(), 0.4f);
  std::filesystem::remove_all(dir);
}

}  // namespace
