// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the editor extension registry and loader: per-module ownership,
// automatic unregistration, and ABI rejection.

#include "extension.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

#include "editor_scene.h"
#include "editor_settings.h"
#include "extension_loader.h"

using namespace Mercury;
using namespace Mercury::EditorUI;

TEST(EditorExtensionRegistry, RemoveModuleOnlyRemovesItsEntries) {
  auto& registry = EditorExtensionRegistry::Instance();
  registry.Clear();

  registry.AddPanel("a", {"a.panel", "A", {}});
  registry.AddMenuItem("a", {"Extensions", "A item", "", {}});
  registry.AddInspector("a", {"Transform", {}});
  registry.AddProjectMenuItem("a", {"A file", {}});
  registry.AddViewportTool("a", {"a.tool", {}, {}, {}});
  registry.AddPanel("b", {"b.panel", "B", {}});
  ASSERT_EQ(registry.Count(), 6u);

  registry.RemoveModule("a");
  EXPECT_EQ(registry.Count(), 1u);
  ASSERT_EQ(registry.Panels().size(), 1u);
  EXPECT_EQ(registry.Panels()[0].module, "b");
  EXPECT_FALSE(registry.CopyInspector("Transform").has_value());

  registry.Clear();
}

TEST(EditorContext, RemovesItsRegistrationsWhenDestroyed) {
  auto& registry = EditorExtensionRegistry::Instance();
  registry.Clear();
  {
    EditorContext first("module-a", nullptr, nullptr, nullptr, nullptr,
                        nullptr);
    first.AddPanel({"a", "A", {}});
    first.AddMenuItem({"Extensions", "A", "", {}});

    EditorContext second("module-b", nullptr, nullptr, nullptr, nullptr,
                         nullptr);
    second.AddPanel({"b", "B", {}});
    EXPECT_EQ(registry.Count(), 3u);
  }
  EXPECT_EQ(registry.Count(), 0u);
  registry.Clear();
}

TEST(EditorExtensionVersion, ComparesMajorMinor) {
  EXPECT_TRUE(IsExtensionVersionCompatible("1.0.0", "1.0.0"));
  EXPECT_TRUE(IsExtensionVersionCompatible("1.0.0-dev", "1.0.3"));
  EXPECT_TRUE(IsExtensionVersionCompatible("1.0.3", "1.0.0-dev"));
  EXPECT_TRUE(IsExtensionVersionCompatible("1.0.0-rc.1", "1.0.2"));
  EXPECT_FALSE(IsExtensionVersionCompatible("1.0.0", "1.1.0"));
  EXPECT_FALSE(IsExtensionVersionCompatible("2.0.0", "1.0.0"));
  EXPECT_FALSE(IsExtensionVersionCompatible("dev", "1.0.0"));
  EXPECT_TRUE(IsExtensionVersionCompatible("dev", "dev"));
}

TEST(EditorSettingsTrust, RemembersDecisionsByCanonicalPath) {
  EditorSettings settings;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "mercury_trust_test";
  std::filesystem::create_directories(root);
  ASSERT_FALSE(settings.HasProjectTrustDecision(root));
  ASSERT_FALSE(settings.IsProjectTrusted(root));

  settings.SetProjectTrusted(root, true);
  EXPECT_TRUE(settings.HasProjectTrustDecision(root));
  EXPECT_TRUE(settings.IsProjectTrusted(root));
  // A spelling that resolves to the same folder shares the decision.
  EXPECT_TRUE(settings.IsProjectTrusted(root / "."));

  settings.SetProjectTrusted(root, false);
  EXPECT_TRUE(settings.HasProjectTrustDecision(root));
  EXPECT_FALSE(settings.IsProjectTrusted(root));
  EXPECT_EQ(settings.distrusted_projects.size(), 1u);
  EXPECT_TRUE(settings.trusted_projects.empty());
  std::filesystem::remove_all(root);
}

TEST(EditorExtensionRegistry, IterationSnapshotsEntries) {
  auto& registry = EditorExtensionRegistry::Instance();
  registry.Clear();
  EditorContext context("module", nullptr, nullptr, nullptr, nullptr, nullptr);
  context.AddPanel({"first", "First", {}});

  int seen = 0;
  registry.ForEachPanel([&](const EditorExtensionRegistry::PanelEntry&) {
    seen++;
    context.AddPanel({"second", "Second", {}});
  });
  // The entry added during the callback is not seen by this pass, and the
  // original loop is not invalidated.
  EXPECT_EQ(seen, 1);
  EXPECT_EQ(registry.Panels().size(), 2u);
  registry.Clear();
}

TEST(EditorExtensionCallbacks, ExceptionsAreContainedAndFaultedModulesStop) {
  auto& registry = EditorExtensionRegistry::Instance();
  registry.Clear();
  registry.SetErrorSink([](std::string_view, std::string_view) {});

  int calls = 0;
  const int value = RunExtensionCallback(
      "module", "test",
      [&]() -> int {
        calls++;
        throw std::runtime_error("boom");
      },
      [] { return 7; });
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(value, 7);

  for (int i = 0; i < 8; i++) {
    RunExtensionCallback("module", "test",
                         [] { throw std::runtime_error("boom"); });
  }
  EXPECT_TRUE(registry.IsModuleFaulted("module"));
  calls = 0;
  RunExtensionCallback("module", "test", [&] { calls++; });
  EXPECT_EQ(calls, 0);

  registry.SetErrorSink({});
  registry.Clear();
}

TEST(EditorMenuIdPool, RecyclesIdsAndReportsExhaustion) {
  EditorMenuIdPool pool(3);
  EXPECT_EQ(pool.Acquire(), 0);
  EXPECT_EQ(pool.Acquire(), 1);
  EXPECT_EQ(pool.Acquire(), 2);
  EXPECT_EQ(pool.Acquire(), -1);

  pool.Release(1);
  EXPECT_EQ(pool.Acquire(), 1);
  EXPECT_EQ(pool.Acquire(), -1);

  pool.Release(0);
  pool.Release(0);  // double release is ignored
  pool.Release(2);
  pool.Release(1);
  // Released slots are reused last-in, first-out.
  EXPECT_EQ(pool.Acquire(), 1);
  EXPECT_EQ(pool.Acquire(), 2);
  EXPECT_EQ(pool.Acquire(), 0);

  pool.Reset();
  EXPECT_EQ(pool.Acquire(), 0);
}

TEST(EditorExtensionLoader, FindsOnlyTheTargetModule) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("mercury_systems_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  const std::filesystem::path module = dir / "game_systems.so";
  std::ofstream(module) << "not really a library";
  std::ofstream(dir / "stray.so") << "stray";

  std::string error;
  EXPECT_EQ(FindSystemModule(dir, "game_systems", &error), module);

  // A symlink is never accepted, even when it points at the expected name.
  std::filesystem::remove(module);
  std::filesystem::create_symlink(dir / "stray.so", module);
  EXPECT_TRUE(FindSystemModule(dir, "game_systems", &error).empty());
  EXPECT_FALSE(error.empty());

  std::filesystem::remove(module);
  EXPECT_TRUE(FindSystemModule(dir, "game_systems", &error).empty());

  std::filesystem::remove_all(dir);
}

TEST(EditorExtensionLoader, RejectsAModuleBuiltForAnotherAbi) {
  EditorScene scene;
  EditorExtensionLoader loader(scene, scene.ConsoleRef());
  std::string error;
  EXPECT_FALSE(loader.LoadFile(MERCURY_TEST_EXTENSION_BAD_ABI_PATH, &error));
  EXPECT_NE(error.find("ABI"), std::string::npos) << error;
  EXPECT_EQ(loader.GetLoadedCount(), 0u);
}

TEST(EditorExtensionLoader, LoadsAndUnloadsAModule) {
  EditorScene scene;
  EditorExtensionLoader loader(scene, scene.ConsoleRef());
  std::string error;
  ASSERT_TRUE(loader.LoadFile(MERCURY_TEST_EXTENSION_PATH, &error)) << error;
  EXPECT_EQ(loader.GetLoadedCount(), 1u);

  loader.UnloadAll();
  EXPECT_EQ(loader.GetLoadedCount(), 0u);
}

TEST(EditorExtensionLoader, ReloadingLoadsAFreshImageOfTheSameModule) {
  EditorScene scene;
  EditorExtensionLoader loader(scene, scene.ConsoleRef());
  std::string error;
  ASSERT_TRUE(loader.LoadFile(MERCURY_TEST_EXTENSION_PATH, &error)) << error;
  // macOS dlopen would hand back the first image for the same path; the
  // loader copies under a new name so a reload sees rebuilt code.
  ASSERT_TRUE(loader.LoadFile(MERCURY_TEST_EXTENSION_PATH, &error)) << error;
  EXPECT_EQ(loader.GetLoadedCount(), 2u);

  loader.UnloadAll();
  EXPECT_EQ(loader.GetLoadedCount(), 0u);
}
