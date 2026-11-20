// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the C++ System registry: phase/order sorting, and the lifecycle a
// MapScene drives.

#include "scripting/system.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "project/project.h"
#include "scene/map_scene.h"

using namespace Mercury;

namespace {

std::vector<std::string> g_log;

class PreSystem : public System {
 public:
  Phase GetPhase() const override { return Phase::kPreTick; }
  int GetOrder() const override { return 10; }
  void OnRegister(MapScene&) override { g_log.push_back("register:Pre"); }
  void OnPreFrame(MapScene&, double) override { g_log.push_back("preframe"); }
  void OnTick(MapScene&, double) override { g_log.push_back("tick:Pre"); }
  void OnShutdown(MapScene&) override { g_log.push_back("shutdown:Pre"); }
};

class TickEarlySystem : public System {
 public:
  int GetOrder() const override { return -5; }
  void OnRegister(MapScene&) override { g_log.push_back("register:TickEarly"); }
  void OnFrame(MapScene&, double) override {
    g_log.push_back("frame:TickEarly");
  }
  void OnTick(MapScene&, double) override { g_log.push_back("tick:TickEarly"); }
};

class TickLateSystem : public System {
 public:
  int GetOrder() const override { return 5; }
  void OnRegister(MapScene&) override { g_log.push_back("register:TickLate"); }
  void OnFrame(MapScene&, double) override {
    g_log.push_back("frame:TickLate");
  }
  void OnTick(MapScene&, double) override { g_log.push_back("tick:TickLate"); }
  void OnLateFrame(MapScene&, double) override { g_log.push_back("lateframe"); }
};

class PostSystem : public System {
 public:
  Phase GetPhase() const override { return Phase::kPostTick; }
  void OnRegister(MapScene&) override { g_log.push_back("register:Post"); }
  void OnTick(MapScene&, double) override { g_log.push_back("tick:Post"); }
};

class PostPhysicsSystem : public System {
 public:
  Phase GetPhase() const override { return Phase::kPostPhysics; }
  void OnRegister(MapScene&) override {
    g_log.push_back("register:PostPhysics");
  }
  void OnTick(MapScene&, double) override {
    g_log.push_back("tick:PostPhysics");
  }
};

class MapAwareSystem : public System {
 public:
  void OnMapLoaded(MapScene&) override { g_log.push_back("map_loaded"); }
  void OnMapUnloading(MapScene&) override { g_log.push_back("map_unloading"); }
};

std::shared_ptr<Project> MakeProject() {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("mercury_system_test_" + std::to_string(std::random_device{}()));
  std::filesystem::create_directories(dir);
  auto project = Project::Create(dir / "test.mproj", "SystemTest");
  project->Save();
  return project;
}

class SystemRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SystemRegistry::Instance().Clear();
    g_log.clear();
  }
  void TearDown() override { SystemRegistry::Instance().Clear(); }
};

}  // namespace

TEST_F(SystemRegistryTest, SortsByPhaseThenOrderThenName) {
  ASSERT_TRUE(RegisterSystem<TickLateSystem>("TickLate"));
  ASSERT_TRUE(RegisterSystem<PostSystem>("Post"));
  ASSERT_TRUE(RegisterSystem<PreSystem>("Pre"));
  ASSERT_TRUE(RegisterSystem<TickEarlySystem>("TickEarly"));
  ASSERT_TRUE(RegisterSystem<PostPhysicsSystem>("PostPhysics"));

  const auto& systems = SystemRegistry::Instance().Systems();
  ASSERT_EQ(systems.size(), 5u);
  EXPECT_EQ(systems[0].name, "Pre");
  EXPECT_EQ(systems[1].name, "TickEarly");
  EXPECT_EQ(systems[2].name, "TickLate");
  EXPECT_EQ(systems[3].name, "Post");
  EXPECT_EQ(systems[4].name, "PostPhysics");
  EXPECT_EQ(systems[1].phase, System::Phase::kTick);
  EXPECT_LT(systems[1].order, systems[2].order);
}

TEST_F(SystemRegistryTest, ReplacesASystemRegisteredUnderTheSameName) {
  ASSERT_TRUE(RegisterSystem<TickEarlySystem>("Duplicated"));
  ASSERT_TRUE(RegisterSystem<PostSystem>("Duplicated"));
  const auto& systems = SystemRegistry::Instance().Systems();
  ASSERT_EQ(systems.size(), 1u);
  EXPECT_EQ(systems[0].phase, System::Phase::kPostTick);
}

TEST_F(SystemRegistryTest, MapSceneDrivesSystemsInPhaseOrder) {
  ASSERT_TRUE(RegisterSystem<TickLateSystem>("TickLate"));
  ASSERT_TRUE(RegisterSystem<PostSystem>("Post"));
  ASSERT_TRUE(RegisterSystem<PreSystem>("Pre"));
  ASSERT_TRUE(RegisterSystem<TickEarlySystem>("TickEarly"));
  ASSERT_TRUE(RegisterSystem<PostPhysicsSystem>("PostPhysics"));

  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kPlay);
  ASSERT_EQ(scene.GetSystems().size(), 0u);

  scene.OnStart();
  ASSERT_EQ(scene.GetSystems().size(), 5u);
  ASSERT_EQ(g_log,
            (std::vector<std::string>{"register:Pre", "register:TickEarly",
                                      "register:TickLate", "register:Post",
                                      "register:PostPhysics"}));

  g_log.clear();
  scene.OnFrame(16.0);
  EXPECT_EQ(g_log, (std::vector<std::string>{"preframe", "frame:TickEarly",
                                             "frame:TickLate", "lateframe"}));

  g_log.clear();
  scene.OnTick(16.0);
  EXPECT_EQ(g_log, (std::vector<std::string>{"tick:Pre", "tick:TickEarly",
                                             "tick:TickLate", "tick:Post"}));

  // Post-physics runs when the application steps the physics world, after
  // OnTick, not from OnTick itself.
  g_log.clear();
  scene.SimulatePhysics(1);
  EXPECT_EQ(g_log, (std::vector<std::string>{"tick:PostPhysics"}));

  g_log.clear();
  scene.OnDestroy();
  EXPECT_EQ(g_log, (std::vector<std::string>{"shutdown:Pre"}));
}

TEST_F(SystemRegistryTest, SystemsSeeMapLoadsAndUnloads) {
  ASSERT_TRUE(RegisterSystem<MapAwareSystem>("MapAware"));

  auto project = MakeProject();
  MapScene scene(project, MapScene::Mode::kPlay);
  scene.OnStart();
  g_log.clear();

  Map map;  // the default map has a single root node
  scene.Instantiate(map);
  EXPECT_EQ(g_log, (std::vector<std::string>{"map_loaded"}));

  g_log.clear();
  scene.ClearMap();
  EXPECT_EQ(g_log, (std::vector<std::string>{"map_unloading"}));
}
