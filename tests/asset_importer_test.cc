// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the asset importer registry: the built-in extension table, user
// registrations, overrides, and the import callback.

#include "project/asset_importer.h"

#include <gtest/gtest.h>

#include <string>

#include "project/asset_database.h"

using namespace Mercury;

namespace {

class AssetImporterTest : public ::testing::Test {
 protected:
  void SetUp() override { AssetImporterRegistry::Instance().Reset(); }
  void TearDown() override { AssetImporterRegistry::Instance().Reset(); }
};

}  // namespace

TEST_F(AssetImporterTest, BuiltinsMatchTheOldHardcodedTable) {
  auto& registry = AssetImporterRegistry::Instance();
  EXPECT_EQ(registry.TypeFromExtension("maps/main.mmap"), "Map");
  EXPECT_EQ(registry.TypeFromExtension("ui/hud.uxml"), "UXML");
  EXPECT_EQ(registry.TypeFromExtension("ui/hud.uss"), "StyleSheet");
  EXPECT_EQ(registry.TypeFromExtension("fonts/inter.ttf"), "Font");
  EXPECT_EQ(registry.TypeFromExtension("models/hero.fbx"), "Model");
  EXPECT_EQ(registry.TypeFromExtension("models/hero.obj"), "Model");
  EXPECT_EQ(registry.TypeFromExtension("art/coin.png"), "Texture");
  EXPECT_EQ(registry.TypeFromExtension("audio/step.wav"), "AudioClip");
  EXPECT_EQ(registry.TypeFromExtension("prefabs/enemy.mprefab"), "Prefab");
  EXPECT_EQ(registry.TypeFromExtension("unknown.thing"), "");
  EXPECT_EQ(registry.TypeFromExtension("no_extension"), "");
}

TEST_F(AssetImporterTest, IsCaseInsensitive) {
  auto& registry = AssetImporterRegistry::Instance();
  EXPECT_EQ(registry.TypeFromExtension("COIN.PNG"), "Texture");
  EXPECT_EQ(registry.TypeFromExtension("HERO.FBX"), "Model");
  const AssetImporterInfo* info = registry.Find("PNG");
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->type, "Texture");
}

TEST_F(AssetImporterTest, UserRegistrationsAreVisibleToTheAssetDatabase) {
  auto& registry = AssetImporterRegistry::Instance();
  registry.Register({{".vox", "voxel"}, "VoxelModel", "Voxel model", {}});

  EXPECT_EQ(registry.TypeFromExtension("models/tree.vox"), "VoxelModel");
  EXPECT_EQ(registry.TypeFromExtension("models/tree.voxel"), "VoxelModel");
  // The old static table would not know either spelling.
  EXPECT_EQ(AssetDatabase::TypeFromExtension("models/tree.vox"), "VoxelModel");
  EXPECT_EQ(AssetDatabase::TypeFromExtension("models/tree.voxel"),
            "VoxelModel");
}

TEST_F(AssetImporterTest, RegisteringAnExtensionAgainReplacesItsType) {
  auto& registry = AssetImporterRegistry::Instance();
  EXPECT_EQ(registry.TypeFromExtension("coin.png"), "Texture");

  registry.Register({{".png"}, "SpriteSheet", "A sliced sprite sheet", {}});
  EXPECT_EQ(registry.TypeFromExtension("coin.png"), "SpriteSheet");

  registry.Reset();
  EXPECT_EQ(registry.TypeFromExtension("coin.png"), "Texture");
}

TEST_F(AssetImporterTest, ImportCallbackFillsProperties) {
  auto& registry = AssetImporterRegistry::Instance();
  registry.Register({{".zed"},
                     "ZedAsset",
                     "Test importer",
                     [](const std::filesystem::path& source,
                        mtext::PropertyList& out, std::string*) {
                       out.Set("source", source.stem().string());
                       return true;
                     }});

  mtext::PropertyList properties;
  ASSERT_TRUE(registry.Import("somewhere/my_asset.zed", properties));
  EXPECT_EQ(properties.GetString("source"), "my_asset");

  // No importer / no callback is a false result with an explanation.
  mtext::PropertyList unused;
  std::string error;
  EXPECT_FALSE(registry.Import("somewhere/my_asset.unknown", unused, &error));
  EXPECT_FALSE(error.empty());

  error.clear();
  EXPECT_FALSE(registry.Import("somewhere/coin.png", unused, &error));
  EXPECT_FALSE(error.empty());
}
