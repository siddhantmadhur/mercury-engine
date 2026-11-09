#include "player.h"

#include <memory>
#include <string>
#include <string_view>

#include "application/default_application.h"
#include "debugger/debugger.h"
#include "os/filesystem.h"
#include "pack/mpack.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/js/script_builder.h"
#include "scripting/js/script_runtime.h"
#include "scripting/ts_types.h"
#include "scripting/type_registry.h"
#include "ui/resources.h"

namespace Mercury {

namespace {

void PrintUsage(const char* program) {
  PDebug::info(
      "usage: {} [project.mproj] [--map res://maps/name.mmap] "
      "[--dump-schema file] [--write-types] [--build-scripts]",
      program);
}

// A game made by the editor's Build Game keeps its assets in .mpack files
// next to the executable. If there are any, mounts them and sets
// `project_path` to the project inside. False if the packs are unusable.
bool MountShippedPacks(std::string& project_path) {
  const fs::path dir = os::ExecutableDir();
  const fs::path game_file = mpack::ArchivePath(dir, mpack::kGamePackName, 0);
  std::error_code ec;
  if (!fs::exists(game_file, ec)) return true;

  std::string error;
  auto game = mpack::Reader::Open(game_file, &error);
  if (!game) {
    PDebug::error("Could not open {}", error);
    return false;
  }
  const fs::path engine_file =
      mpack::ArchivePath(dir, mpack::kEnginePackName, 0);
  auto engine = mpack::Reader::Open(engine_file, &error);
  if (!engine) {
    PDebug::error("Could not open {}", error);
    return false;
  }
  std::string mproj;
  for (const auto& entry : game->GetEntries()) {
    if (entry.path.find('/') == std::string::npos &&
        entry.path.ends_with(".mproj")) {
      mproj = entry.path;
      break;
    }
  }
  if (mproj.empty()) {
    PDebug::error("{} has no .mproj", game_file.string());
    return false;
  }

  os::Mount(dir, game);
  os::Mount(UI::UIResources::EngineResourceDir(), engine,
            std::string(mpack::kEngineUiFolder));
#ifdef MERCURY_SPIRV_PATH
  os::Mount(MERCURY_SPIRV_PATH, engine,
            std::string(mpack::kEngineShaderFolder));
#endif
  project_path = (dir / mproj).string();
  PDebug::info("Running from {}", game_file.string());
  return true;
}

}  // namespace

int RunPlayer(int argc, char** argv, const char* default_project) {
  std::string project_path = default_project ? default_project : "";
  std::string map_path;
  std::string schema_path;
  bool project_given = false;
  bool write_types = false;
  bool build_scripts = false;

  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    auto next = [&]() -> const char* {
      return i + 1 < argc ? argv[++i] : nullptr;
    };
    if (arg == "--map") {
      if (auto value = next()) map_path = value;
    } else if (arg == "--dump-schema") {
      if (auto value = next()) schema_path = value;
    } else if (arg == "--write-types") {
      write_types = true;
    } else if (arg == "--build-scripts") {
      build_scripts = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else if (!arg.starts_with("--")) {
      project_path = os::UserPath(std::string(arg)).string();
      project_given = true;
    } else {
      PDebug::error("Unknown option {}", arg);
      PrintUsage(argv[0]);
      return 2;
    }
  }

  RegisterBuiltinTypes();

  if (!project_given && !MountShippedPacks(project_path)) return 1;
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
  const fs::path root = project->GetRoot();
  // Loose files (not a shipped game): the scripts may need building.
  const bool loose = !os::IsPacked(root / ".mercury" / "scripts" / "game.js");

  if (write_types) {
    if (!WriteDefaultTsconfig(root, &error) || !WriteTypeScriptTypes(root / ".mercury" / "types", &error)) {
      PDebug::error("{}", error);
      return 1;
    }
    PDebug::info("Wrote {}", (root / ".mercury" / "types").string());
    return 0;
  }
  if (loose && (build_scripts || !ScriptBuilder::IsUpToDate(root))) {
    BuildOptions options;
    options.on_line = [](const std::string& line) { PDebug::info("{}", line); };
    if (!ScriptBuilder::Build(root, options)) {
      PDebug::error("Building the scripts failed");
      return 1;
    }
  }
  if (build_scripts) return 0;

  // V8 lives from here to the end of the function; the scene and its scripts are destroyed first.
  struct RuntimeGuard {
    ~RuntimeGuard() { ScriptRuntime::Instance().Shutdown(); }
  };
  if (!ScriptRuntime::Instance().Initialize()) {
    PDebug::error("Could not start the script engine");
    return 1;
  }
  RuntimeGuard runtime_guard;
  const fs::path bundle = ScriptBuilder::BundlePath(root);
  if (os::Exists(bundle)) {
    if (!ScriptRuntime::Instance().LoadBundle(bundle.string(),
                                              ScriptBuilder::SourceMapPath(root).string())) {
      PDebug::error("Could not load the scripts");
      return 1;
    }
  } else {
    PDebug::warn("{} not found: running without scripts", bundle.string());
  }

  if (!schema_path.empty()) {
    if (!TypeRegistry::Instance().SaveSchema(schema_path)) {
      PDebug::error("Could not write schema to {}", schema_path);
      return 1;
    }
    return 0;
  }

  if (map_path.empty()) map_path = project->GetMainMap();
  if (map_path.empty()) {
    PDebug::error("{} has no main_map and no --map was given",
                  project->GetFile().string());
    return 1;
  }

  auto application = std::make_unique<DefaultApplication>(
      project->GetWindowTitle(),
      static_cast<float>(project->GetWindowWidth()),
      static_cast<float>(project->GetWindowHeight()));
  auto scene = std::make_unique<MapScene>(project, MapScene::Mode::kPlay);

  Map map;
  if (!map.Load(project->Resolve(map_path), &error,
                scene->GetPrefabCache().Loader())) {
    PDebug::error("Could not load map: {}", error);
    return 1;
  }
  // Models load on background threads while the window shows black.
  scene->BeginInstantiate(std::move(map));
  application->SetGameScene(std::move(scene));
  const int result = application->Run();
  // The scene (and so every script) goes before the script engine does.
  application.reset();
  return result;
}

}  // namespace Mercury
