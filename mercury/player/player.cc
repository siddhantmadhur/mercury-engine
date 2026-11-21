// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "player.h"

#include <charconv>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "application/default_application.h"
#include "debugger/debugger.h"
#include "introspection/introspection.h"
#include "os/filesystem.h"
#include "pack/mpack.h"
#include "project/map.h"
#include "project/project.h"
#include "rust/bridge.h"
#include "scene/map_scene.h"
#include "scripting/app_hook.h"
#include "scripting/as/script_builder.h"
#include "scripting/as/script_runtime.h"
#include "scripting/type_registry.h"
#include "ui/resources.h"

namespace Mercury {

namespace {

void PrintUsage(const char* program) {
  PDebug::info(
      "usage: {} [project.mproj] [--map res://maps/name.mmap] "
      "[--dump-schema file] [--build-scripts] "
      "[--dump-engine-schema [file|-]] [--validate [--json]] "
      "[--inspect-asset res://asset] [--headless] [--frames N]",
      program);
}

// A game made by the editor's Build Game keeps its assets in .mpack files
// next to the executable. If there are any, mounts them and sets
// `project_path` to the project inside. False if the packs are unusable.
bool MountShippedPacks(std::string& project_path) {
  fs::path dir = os::ExecutableDir();
  // A .app bundle keeps the packs in Contents/Resources instead of next to the
  // executable.
  const fs::path resources = os::BundleResourcesDir();
  std::error_code ec;
  if (!resources.empty() &&
      fs::exists(mpack::ArchivePath(resources, mpack::kGamePackName, 0), ec)) {
    dir = resources;
  }
  const fs::path game_file = mpack::ArchivePath(dir, mpack::kGamePackName, 0);
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
  os::Mount(os::EngineMaterialDir(), engine,
            std::string(mpack::kEngineMaterialFolder));
  project_path = (dir / mproj).string();
  PDebug::info("Running from {}", game_file.string());
  return true;
}

}  // namespace

PlayerArgs ParsePlayerArgs(int argc, char** argv, bool allow_unknown) {
  PlayerArgs args;
  // Set after an option the player does not know: the tokens that follow are
  // that option's values, not the project path.
  bool after_unknown = false;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    auto next = [&]() -> const char* {
      return i + 1 < argc ? argv[++i] : nullptr;
    };
    if (arg == "--map") {
      if (auto value = next()) args.map_path = value;
    } else if (arg == "--dump-schema") {
      if (auto value = next()) args.schema_path = value;
    } else if (arg == "--dump-engine-schema") {
      // A file name has to be a bare token that is not another option;
      // "-", nothing at all or a following --option all mean stdout. The
      // token is consumed only when it is taken, unlike next().
      if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--")) {
        args.engine_schema_path = argv[++i];
      } else {
        args.engine_schema_path = "-";
      }
    } else if (arg == "--inspect-asset") {
      if (auto value = next()) {
        args.inspect_asset = value;
      } else {
        args.option_error = "--inspect-asset needs a res:// asset path";
      }
    } else if (arg == "--frames") {
      const char* value = next();
      if (value == nullptr) {
        args.option_error = "--frames needs a frame count";
      } else {
        const std::string_view text = value;
        int frames = 0;
        const auto [end, ec] =
            std::from_chars(text.data(), text.data() + text.size(), frames);
        if (ec != std::errc{} || end != text.data() + text.size() ||
            frames < 0) {
          args.option_error = "--frames needs a frame count (got " +
                              std::string(text) + ")";
        } else {
          args.frames = frames;
        }
      }
    } else if (arg == "--build-scripts") {
      args.build_scripts = true;
    } else if (arg == "--validate") {
      args.validate = true;
    } else if (arg == "--json") {
      args.json = true;
    } else if (arg == "--headless") {
      args.headless = true;
    } else if (arg == "--help" || arg == "-h") {
      args.help = true;
    } else if (!arg.starts_with("--")) {
      if (after_unknown) continue;
      args.project_path = os::UserPath(std::string(arg)).string();
      args.project_given = true;
    } else if (!allow_unknown) {
      args.unknown_option = std::string(arg);
      return args;
    } else {
      after_unknown = true;
    }
  }
  return args;
}

int RunPlayer(int argc, char** argv, const char* default_project) {
  // A game with an app hook owns the options the player does not know; the
  // hook sees them in OnPreInit.
  const bool has_app_hooks = !AppHookRegistry::Instance().Hooks().empty();
  const PlayerArgs parsed = ParsePlayerArgs(argc, argv, has_app_hooks);
  if (!parsed.unknown_option.empty()) {
    PDebug::error("Unknown option {}", parsed.unknown_option);
    PrintUsage(argv[0]);
    return 2;
  }
  if (!parsed.option_error.empty()) {
    PDebug::error("{}", parsed.option_error);
    PrintUsage(argv[0]);
    return 2;
  }
  if (parsed.json && !parsed.validate) {
    PDebug::error("--json only applies to --validate");
    PrintUsage(argv[0]);
    return 2;
  }
  if (parsed.help) {
    PrintUsage(argv[0]);
    return 0;
  }
  std::string project_path = parsed.project_given
                                 ? parsed.project_path
                                 : (default_project ? default_project : "");
  std::string map_path = parsed.map_path;
  std::string schema_path = parsed.schema_path;
  std::string engine_schema_path = parsed.engine_schema_path;
  std::string inspect_asset = parsed.inspect_asset;
  const int frames = parsed.frames;
  const bool project_given = parsed.project_given;
  const bool build_scripts = parsed.build_scripts;
  const bool validate = parsed.validate;
  const bool json = parsed.json;
  const bool headless = parsed.headless;

  RegisterBuiltinTypes();
  Mercury::rust::Init();

  // The engine's own component types as JSON, for MCP clients and tooling.
  // They do not depend on the project, so this works without one.
  if (!engine_schema_path.empty()) {
    const std::string schema = introspection::EngineSchemaJson();
    if (engine_schema_path == "-") {
      std::cout << schema << "\n";
    } else if (!os::WriteFileAtomic(engine_schema_path, schema)) {
      PDebug::error("Could not write engine schema to {}",
                    engine_schema_path);
      return 1;
    }
    return 0;
  }

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

  // AngelScript lives from here to the end of the function; the scene and its
  // scripts are destroyed first.
  struct RuntimeGuard {
    ~RuntimeGuard() { ScriptRuntime::Instance().Shutdown(); }
  } runtime_guard;
  if (!ScriptRuntime::Instance().Initialize()) {
    PDebug::error("Could not start the script engine");
    return 1;
  }

  BuildOptions options;
  options.on_line = [](const std::string& line) { PDebug::info("{}", line); };
  if (!ScriptBuilder::Build(root, options)) {
    PDebug::error("Building the scripts failed");
    return 1;
  }
  if (build_scripts) return 0;

  if (!schema_path.empty()) {
    if (!TypeRegistry::Instance().SaveSchema(schema_path)) {
      PDebug::error("Could not write schema to {}", schema_path);
      return 1;
    }
    return 0;
  }

  if (validate) {
    const introspection::ValidationReport report =
        introspection::ValidateProject(*project);
    std::cout << (json ? report.ToJson() : report.ToText()) << "\n";
    return report.HasErrors() ? 1 : 0;
  }
  if (!inspect_asset.empty()) {
    std::string asset_json;
    std::string error;
    if (!introspection::InspectAsset(*project, inspect_asset, &asset_json,
                                     &error)) {
      PDebug::error("{}", error);
      return 1;
    }
    std::cout << asset_json << "\n";
    return 0;
  }

  // Process-wide game hooks. They are created only for a real run: the
  // script-build and schema-dump paths above must not start a game's
  // subsystems.
  std::vector<std::string_view> hook_args;
  hook_args.reserve(static_cast<size_t>(argc > 1 ? argc - 1 : 0));
  for (int i = 1; i < argc; i++) hook_args.emplace_back(argv[i]);
  std::vector<std::unique_ptr<AppHook>> app_hooks =
      AppHookRegistry::Instance().CreateAll();
  // Every created hook gets OnShutdown, including when a later hook or the
  // game setup fails.
  struct AppHookGuard {
    std::vector<std::unique_ptr<AppHook>>* hooks;
    ~AppHookGuard() {
      for (auto it = hooks->rbegin(); it != hooks->rend(); ++it) {
        (*it)->OnShutdown();
      }
    }
  } hook_guard{&app_hooks};
  for (auto& hook : app_hooks) {
    if (!hook->OnPreInit(hook_args)) {
      PDebug::error("A game app hook rejected the command line");
      return 1;
    }
  }
  for (auto& hook : app_hooks) {
    if (!hook->OnInit()) {
      PDebug::error("A game app hook failed to initialize");
      return 1;
    }
  }

  if (map_path.empty()) map_path = project->GetMainMap();
  if (map_path.empty()) {
    PDebug::error("{} has no main_map and no --map was given",
                  project->GetFile().string());
    return 1;
  }

  auto application = std::make_unique<DefaultApplication>(
      project->GetWindowTitle(), static_cast<float>(project->GetWindowWidth()),
      static_cast<float>(project->GetWindowHeight()));
  // Offscreen rendering: no window or swapchain, frames still render (MCP
  // clients and tests read screenshots back through the editor's hosted API).
  if (headless) application->EnableHeadlessMode(true);
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
  if (frames > 0) {
    // The same machinery the benchmark and scripted input drive Run() with:
    // StartHosted() is Run()'s setup and StepFrame() its loop body, without
    // the window-close check, so the run ends after exactly N frames.
    if (!application->StartHosted()) {
      PDebug::error("Could not initialize application");
      return 1;
    }
    for (int i = 0; i < frames; i++) application->StepFrame();
    application.reset();
    return 0;
  }
  const int result = application->Run();
  // The scene (and so every script) goes before the script engine does.
  application.reset();
  return result;
}

}  // namespace Mercury
