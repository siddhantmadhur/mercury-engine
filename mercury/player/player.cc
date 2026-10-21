#include "player.h"

#include <memory>
#include <string>
#include <string_view>

#include "application/default_application.h"
#include "debugger/debugger.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace {

void PrintUsage(const char* program) {
  PDebug::info(
      "usage: {} [project.mproj] [--map res://maps/name.mmap] "
      "[--dump-schema file]",
      program);
}

}  // namespace

int RunPlayer(int argc, char** argv, const char* default_project) {
  std::string project_path = default_project ? default_project : "";
  std::string map_path;
  std::string schema_path;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    auto next = [&]() -> const char* {
      return i + 1 < argc ? argv[++i] : nullptr;
    };
    if (arg == "--map") {
      if (auto value = next()) map_path = value;
    } else if (arg == "--dump-schema") {
      if (auto value = next()) schema_path = value;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else if (!arg.starts_with("--")) {
      project_path = std::string(arg);
    } else {
      PDebug::error("Unknown option {}", arg);
      PrintUsage(argv[0]);
      return 2;
    }
  }

  RegisterBuiltinTypes();

  if (!schema_path.empty()) {
    if (!TypeRegistry::Instance().SaveSchema(schema_path)) {
      PDebug::error("Could not write schema to {}", schema_path);
      return 1;
    }
    return 0;
  }

  if (project_path.empty()) {
    PDebug::error("No project given");
    PrintUsage(argv[0]);
    return 2;
  }

  std::string error;
  auto project = Project::Load(project_path, &error);
  if (!project) {
    PDebug::error("Could not open project: {}", error);
    return 1;
  }
  if (map_path.empty()) map_path = project->GetMainMap();
  if (map_path.empty()) {
    PDebug::error("{} has no main_map and no --map was given",
                  project->GetFile().string());
    return 1;
  }

  Map map;
  if (!map.Load(project->Resolve(map_path), &error)) {
    PDebug::error("Could not load map: {}", error);
    return 1;
  }

  auto application = std::make_unique<DefaultApplication>(
      project->GetWindowTitle(),
      static_cast<float>(project->GetWindowWidth()),
      static_cast<float>(project->GetWindowHeight()));
  auto scene = std::make_unique<MapScene>(project, MapScene::Mode::kPlay);
  scene->Instantiate(map);
  application->SetGameScene(std::move(scene));
  return application->Run();
}

}  // namespace Mercury
