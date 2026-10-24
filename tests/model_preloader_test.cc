// Tests for loading a map's models on background threads.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scene/model_preloader.h"
#include "scripting/type_registry.h"

using namespace Mercury;

namespace {

const char* kTriangle =
    "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nvt 0 0\nvt 1 0\nvt 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\n";

// A project with `count` triangle models and a map that draws each one.
class PreloadProject {
 public:
  explicit PreloadProject(const char* name, int count = 3) {
    dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "maps");
    std::filesystem::create_directories(dir / "models");
    std::ofstream(dir / "test.mproj") << "[project]\nname = \"Test\"\n";
    std::string map = "[mmap format=1]\n[node name=\"Root\"]\n";
    for (int i = 0; i < count; i++) {
      const std::string id = std::to_string(i);
      std::ofstream(dir / ("models/m" + id + ".obj")) << kTriangle;
      map += "[node name=\"N" + id + "\" parent=\".\"]\n";
      map += "[component type=\"MeshRenderer\" node=\"N" + id + "\"]\n";
      map += "mesh = \"model\"\nmodel = Resource(\"res://models/m" + id + ".obj\")\n";
    }
    // Shapes and a model on the same node are not model loads.
    map += "[node name=\"Cube\" parent=\".\"]\n[component type=\"MeshRenderer\" node=\"Cube\"]\n";
    map += "model = Resource(\"res://models/m0.obj\")\n";
    std::ofstream(dir / "maps/a.mmap") << map;
    std::string error;
    project = Project::Load(dir / "test.mproj", &error);
    EXPECT_TRUE(project) << error;
    EXPECT_TRUE(this->map.Load(dir / "maps/a.mmap", &error)) << error;
  }
  ~PreloadProject() { std::filesystem::remove_all(dir); }

  std::filesystem::path dir;
  std::shared_ptr<Project> project;
  Map map;
};

bool WaitFor(const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!done()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

}  // namespace

TEST(ModelPreloader, LoadsEveryJobAndReportsFailures) {
  PreloadProject files("mercury_preloader_test", 4);
  std::vector<ModelPreloader::Job> jobs;
  for (int i = 0; i < 4; i++) {
    const std::string id = std::to_string(i);
    jobs.push_back({"res://models/m" + id + ".obj",
                    (files.dir / ("models/m" + id + ".obj")).string(), {}});
  }
  jobs.push_back({"res://missing.obj", (files.dir / "missing.obj").string(), {}});

  ModelPreloader loader;
  EXPECT_FALSE(loader.IsBusy());
  loader.Start(std::move(jobs));
  EXPECT_TRUE(loader.IsBusy());
  EXPECT_EQ(loader.Total(), 5u);
  ASSERT_TRUE(WaitFor([&] { return loader.IsFinished(); }));
  EXPECT_EQ(loader.Done(), 5u);

  auto results = loader.TakeResults();
  EXPECT_FALSE(loader.IsBusy());
  ASSERT_EQ(results.size(), 5u);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(results[i].res_path, "res://models/m" + std::to_string(i) + ".obj");
    EXPECT_TRUE(results[i].model);
  }
  EXPECT_EQ(results[4].res_path, "res://missing.obj");
  EXPECT_FALSE(results[4].model);
}

TEST(ModelPreloader, CancelStopsAndDropsResults) {
  PreloadProject files("mercury_preloader_cancel_test", 3);
  std::vector<ModelPreloader::Job> jobs;
  for (int i = 0; i < 3; i++) {
    jobs.push_back({"m", (files.dir / ("models/m" + std::to_string(i) + ".obj")).string(), {}});
  }
  ModelPreloader loader;
  loader.Start(std::move(jobs));
  loader.Cancel();
  EXPECT_FALSE(loader.IsBusy());
  EXPECT_EQ(loader.Total(), 0u);
  EXPECT_TRUE(loader.TakeResults().empty());
}

TEST(ModelPreloader, MissingModelsListsRenderedModelsOnce) {
  RegisterBuiltinTypes();
  PreloadProject files("mercury_missing_models_test", 3);
  MapScene scene(files.project, MapScene::Mode::kEdit);

  auto jobs = scene.MissingModels(files.map);
  ASSERT_EQ(jobs.size(), 3u);  // the cube's unused model is not listed
  EXPECT_EQ(jobs[0].res_path, "res://models/m0.obj");

  scene.StartModelPreload(std::move(jobs));
  ASSERT_TRUE(WaitFor([&] { return scene.PollModelPreload(); }));
  EXPECT_FALSE(scene.IsModelPreloading());
  EXPECT_TRUE(scene.MissingModels(files.map).empty());
  EXPECT_EQ(scene.MissingModels(files.map, true).size(), 3u);
}

TEST(ModelPreloader, BeginInstantiateWaitsForModels) {
  RegisterBuiltinTypes();
  PreloadProject files("mercury_begin_instantiate_test", 3);
  MapScene scene(files.project, MapScene::Mode::kPlay);
  scene.BeginInstantiate(files.map);
  EXPECT_TRUE(scene.IsLoadingMap());
  EXPECT_FALSE(scene.Find("N0").IsValid());
  scene.OnStart();

  ASSERT_TRUE(WaitFor([&] {
    scene.OnFrame(1000.0 / 60.0);
    return !scene.IsLoadingMap();
  }));
  for (const char* name : {"N0", "N1", "N2"}) {
    ASSERT_TRUE(scene.Find(name).IsValid()) << name;
    auto* mesh = scene.Objects().GetProperty<RenderMesh>(scene.Find(name).GetEntity());
    ASSERT_TRUE(mesh) << name;
    EXPECT_TRUE(mesh->model) << name;
  }
  // Everything is cached now, so a second call instantiates at once.
  scene.BeginInstantiate(files.map);
  EXPECT_FALSE(scene.IsLoadingMap());
}
