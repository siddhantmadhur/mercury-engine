// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the editor's AngelScript pieces: Create Script, new projects and the
// Console's clickable script positions. Links the editor library, so it is a
// separate executable.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "cmake_build.h"
#include "console.h"
#include "editor_scene.h"
#include "os/filesystem.h"
#include "os/process.h"
#include "project/project.h"
#include "project_scaffold.h"
#include "scripting/as/script_builder.h"
#include "scripting/as/script_runtime.h"
#include "templates.h"
#include "test_paths.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {
fs::path WorkDir(const char* name) {
  const fs::path dir = Mercury::test::CacheDir() / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string Relative(const fs::path& root, const fs::path& file) {
  return fs::relative(file, root).generic_string();
}
}  // namespace

TEST(EditorTemplates, NewProjectHasScriptsAndGeneratedBuildFiles) {
  const fs::path parent = WorkDir("editor_new_project");
  std::string error;
  const fs::path mproj =
      Templates::CreateProject(parent, "My Game", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();
  EXPECT_TRUE(fs::is_directory(root / "scripts"));
  EXPECT_TRUE(fs::is_directory(root / "src"));
  EXPECT_TRUE(fs::is_directory(root / "system"));

  // Phase 4: every project gets its own CMake tree and LSP files. The editor
  // owns these only until they exist; CMakeLists.txt and the presets are then
  // the project's.
  const std::string cmake = Read(root / "CMakeLists.txt");
  EXPECT_NE(cmake.find("mercury_add_game(my_game"), std::string::npos) << cmake;
  EXPECT_TRUE(fs::exists(root / "CMakePresets.json"));
  const std::string clangd = Read(root / ".clangd");
  EXPECT_NE(clangd.find("CompilationDatabase: .mercury/build/debug"),
            std::string::npos)
      << clangd;

  const std::string ignores = Read(root / ".gitignore");
  EXPECT_NE(ignores.find(".mercury/"), std::string::npos);
  EXPECT_NE(ignores.find(".clangd"), std::string::npos);
  EXPECT_NE(ignores.find("compile_commands.json"), std::string::npos);
}

TEST(EditorTemplates, CreateScriptWritesAnAngelScriptClassThatBuilds) {
  const fs::path parent = WorkDir("editor_create_script");
  std::string error;
  const fs::path mproj = Templates::CreateProject(parent, "Demo", true, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();

  const fs::path script = Templates::CreateScript(root, "PlayerMover", &error);
  ASSERT_FALSE(script.empty()) << error;
  EXPECT_EQ(script.filename(), "player_mover.as");
  const std::string text = Read(script);
  EXPECT_NE(text.find("[script]"), std::string::npos);
  EXPECT_NE(text.find("class PlayerMover : Behaviour"), std::string::npos);
  EXPECT_NE(text.find("[field tooltip="), std::string::npos);
  EXPECT_NE(text.find("float speed = 1.0f;"), std::string::npos);

  // A second script with the same name is refused, and a bad name too.
  EXPECT_TRUE(Templates::CreateScript(root, "PlayerMover", &error).empty());
  EXPECT_TRUE(Templates::CreateScript(root, "not valid", &error).empty());
  EXPECT_NE(error.find("AngelScript class name"), std::string::npos);

  // The template compiles with the embedded AngelScript compiler.
  std::string log;
  EXPECT_TRUE(ScriptBuilder::Build(root, {}, &log)) << log;
  EXPECT_NE(ScriptRuntime::Instance().FindScriptClass("PlayerMover"), nullptr);
}

TEST(EditorConsole, ScriptPositionsAreClickable) {
  EXPECT_EQ(Console::FindScriptFile("scripts/ball.as:42:5: Error: boom"),
            std::optional<std::string>("scripts/ball.as"));
  EXPECT_EQ(Console::FindScriptFile("scripts/sub/ball.as(12,3): error: bad"),
            std::optional<std::string>("scripts/sub/ball.as"));
  EXPECT_EQ(Console::FindScriptFile("saved scripts/ folder"), std::nullopt);
  EXPECT_EQ(Console::FindScriptFile("scripts/ball.as is fine"), std::nullopt);
}

TEST(EditorViewportNavigation, MovesIn2DAnd3DWithArrowsAndWasdqe) {
  EditorScene scene;
  scene.SetView2D(true);
  const glm::vec3 t0 = scene.GetCameraTarget();

  // 2D arrow-key panning works without holding mouse buttons.
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, false, false,
                                 EditorScene::kNavRight | EditorScene::kNavUp,
                                 false, 0.5f);
  const glm::vec3 t_arrow = scene.GetCameraTarget();
  EXPECT_GT(t_arrow.x, t0.x);
  EXPECT_GT(t_arrow.y, t0.y);
  EXPECT_FLOAT_EQ(t_arrow.z, t0.z);

  // Holding Shift increases 2D navigation speed.
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, false, false,
                                 EditorScene::kNavRight, false, 0.2f);
  const float dx_normal = scene.GetCameraTarget().x - t_arrow.x;
  const glm::vec3 t_mid = scene.GetCameraTarget();
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, false, false,
                                 EditorScene::kNavRight, true, 0.2f);
  const float dx_shift = scene.GetCameraTarget().x - t_mid.x;
  EXPECT_NEAR(dx_shift, dx_normal * 2.5f, 1e-3f);

  // 2D RMB + WASD pans in addition to arrow keys.
  const glm::vec3 t_rmb0 = scene.GetCameraTarget();
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, true, false,
                                 EditorScene::kFlyA | EditorScene::kFlyS, false,
                                 0.5f);
  EXPECT_LT(scene.GetCameraTarget().x, t_rmb0.x);
  EXPECT_LT(scene.GetCameraTarget().y, t_rmb0.y);

  // 3D arrow keys move forward/right along the view even without RMB.
  scene.SetView2D(false);
  const glm::vec3 t3_0 = scene.GetCameraTarget();
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, false, false,
                                 EditorScene::kNavUp, false, 0.5f);
  const glm::vec3 d_fwd = scene.GetCameraTarget() - t3_0;
  EXPECT_GT(glm::length(d_fwd), 1.0f);
  EXPECT_LT(d_fwd.z, 0.0f);  // Default 3D yaw looks toward -Z

  // 3D RMB + WASDQE vertical E/Q movement and Shift 3x multiplier.
  const glm::vec3 t3_e0 = scene.GetCameraTarget();
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, true, false,
                                 EditorScene::kFlyE, false, 0.25f);
  const float dy_normal = scene.GetCameraTarget().y - t3_e0.y;
  EXPECT_GT(dy_normal, 1.0f);

  const glm::vec3 t3_e1 = scene.GetCameraTarget();
  scene.UpdateViewportNavigation({0.0f, 0.0f}, false, true, false,
                                 EditorScene::kFlyE, true, 0.25f);
  const float dy_shift = scene.GetCameraTarget().y - t3_e1.y;
  EXPECT_NEAR(dy_shift, dy_normal * 3.0f, 1e-3f);
}

TEST(EditorTemplates, ScriptKindsWriteTheRightFilesAndContent) {
  const fs::path parent = WorkDir("editor_script_kinds");
  std::string error;
  const fs::path mproj =
      Templates::CreateProject(parent, "Script Kinds", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();

  auto check = [&](Templates::ScriptKind kind, const char* name,
                   const char* expected_header_text,
                   const char* expected_source_text) {
    const Templates::ScriptPaths paths =
        Templates::GetScriptPaths(root, kind, name);
    const fs::path source = Templates::CreateScript(root, name, kind, &error);
    if (source.empty()) {
      ADD_FAILURE() << error;
      return paths;
    }
    EXPECT_EQ(source, paths.source);
    EXPECT_TRUE(fs::is_regular_file(paths.source));
    EXPECT_EQ(paths.header.empty(),
              kind == Templates::ScriptKind::kAngelScript);
    if (!paths.header.empty()) {
      EXPECT_TRUE(fs::is_regular_file(paths.header));
      EXPECT_EQ(Relative(root, paths.header),
                std::string(kind == Templates::ScriptKind::kCppEditorSystem
                                ? "system/"
                                : "src/") +
                    Templates::ToSnakeCase(name) + ".h");
      EXPECT_NE(Read(paths.header).find(expected_header_text),
                std::string::npos)
          << Read(paths.header);
    }
    EXPECT_NE(Read(paths.source).find(expected_source_text), std::string::npos)
        << Read(paths.source);
    return paths;
  };

  const Templates::ScriptPaths behaviour =
      check(Templates::ScriptKind::kCppBehaviour, "PlayerController",
            "class PlayerController : public Mercury::Behaviour",
            "MERCURY_SCRIPT(PlayerController);");
  EXPECT_NE(Read(behaviour.header).find("#pragma once"), std::string::npos);
  EXPECT_NE(Read(behaviour.header).find("<scripting/behaviour.h>"),
            std::string::npos);
  EXPECT_NE(Read(behaviour.header).find("void OnFrame(double dt) override;"),
            std::string::npos);
  EXPECT_NE(Read(behaviour.header).find("Reflect"), std::string::npos);
  EXPECT_NE(Read(behaviour.source).find("OnStart()"), std::string::npos);

  const Templates::ScriptPaths system =
      check(Templates::ScriptKind::kCppSystem, "NetworkSystem",
            "class NetworkSystem : public Mercury::System",
            "MERCURY_SYSTEM(NetworkSystem);");
  EXPECT_NE(Read(system.header).find("kPostPhysics"), std::string::npos);
  EXPECT_NE(Read(system.source).find("OnTick"), std::string::npos);

  const Templates::ScriptPaths editor =
      check(Templates::ScriptKind::kCppEditorSystem, "MyTool",
            "public Mercury::EditorUI::EditorExtension",
            "MERCURY_EDITOR_EXTENSION(MyTool);");
  EXPECT_NE(Read(editor.source).find("AddMenuItem"), std::string::npos);

  const Templates::ScriptPaths script =
      check(Templates::ScriptKind::kAngelScript, "PlayerMover", "",
            "class PlayerMover : Behaviour");
  EXPECT_EQ(Relative(root, script.source), "scripts/player_mover.as");
  // C++ kinds never write into scripts/.
  EXPECT_FALSE(fs::exists(root / "scripts" / "player_controller.cc"));
}

TEST(EditorTemplates, CppScriptsNeverOverwrite) {
  const fs::path parent = WorkDir("editor_script_no_overwrite");
  std::string error;
  const fs::path mproj =
      Templates::CreateProject(parent, "No Overwrite", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();

  const Templates::ScriptPaths paths = Templates::GetScriptPaths(
      root, Templates::ScriptKind::kCppBehaviour, "PlayerController");
  ASSERT_FALSE(Templates::CreateScript(root, "PlayerController",
                                       Templates::ScriptKind::kCppBehaviour,
                                       &error)
                   .empty())
      << error;
  const std::string header_before = Read(paths.header);

  EXPECT_TRUE(Templates::CreateScript(root, "PlayerController",
                                      Templates::ScriptKind::kCppBehaviour,
                                      &error)
                  .empty());
  EXPECT_NE(error.find("already exists"), std::string::npos) << error;
  EXPECT_EQ(Read(paths.header), header_before);

  // A file that exists on its own (header only) blocks creation and leaves no
  // .cc behind.
  const Templates::ScriptPaths taken = Templates::GetScriptPaths(
      root, Templates::ScriptKind::kCppSystem, "Taken");
  fs::create_directories(taken.header.parent_path());
  std::ofstream(taken.header) << "// mine\n";
  EXPECT_TRUE(Templates::CreateScript(root, "Taken",
                                      Templates::ScriptKind::kCppSystem, &error)
                  .empty());
  EXPECT_FALSE(fs::exists(taken.source));
  EXPECT_EQ(Read(taken.header), "// mine\n");
}

TEST(EditorTemplates, CppScriptNamesAreValidated) {
  const fs::path parent = WorkDir("editor_script_identifiers");
  std::string error;
  const fs::path mproj =
      Templates::CreateProject(parent, "Identifiers", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();

  const std::string too_long(65, 'A');
  std::vector<std::string> names = {
      "1Bad", "bad name", "bad-name",  "",        "class",
      "and",  "System",   "Behaviour", "_hidden", "has__two"};
  names.push_back(too_long);
  for (const std::string& name : names) {
    error.clear();
    EXPECT_TRUE(Templates::CreateScript(
                    root, name, Templates::ScriptKind::kCppSystem, &error)
                    .empty())
        << name;
    EXPECT_NE(error.find("C++ class name"), std::string::npos) << error;
  }
  EXPECT_TRUE(fs::is_empty(root / "src"));
}

TEST(EditorTemplates, RefusesSymlinkedContentFolders) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test uses symlinks";
#else
  const fs::path parent = WorkDir("editor_script_symlink");
  std::string error;
  const fs::path mproj =
      Templates::CreateProject(parent, "Symlink", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();
  const fs::path outside = WorkDir("editor_script_symlink_outside");

  fs::remove_all(root / "src");
  fs::create_symlink(outside, root / "src");
  error.clear();
  EXPECT_TRUE(Templates::CreateScript(root, "PlayerController",
                                      Templates::ScriptKind::kCppBehaviour,
                                      &error)
                  .empty());
  EXPECT_NE(error.find("symlink"), std::string::npos) << error;
  EXPECT_TRUE(fs::is_empty(outside));

  fs::remove_all(root / "system");
  fs::create_symlink(outside, root / "system");
  error.clear();
  EXPECT_TRUE(Templates::CreateScript(root, "MyTool",
                                      Templates::ScriptKind::kCppEditorSystem,
                                      &error)
                  .empty());
  EXPECT_NE(error.find("symlink"), std::string::npos) << error;
  EXPECT_TRUE(fs::is_empty(outside));

  fs::remove_all(root / "scripts");
  fs::create_symlink(outside, root / "scripts");
  error.clear();
  EXPECT_TRUE(Templates::CreateScript(root, "PlayerMover",
                                      Templates::ScriptKind::kAngelScript,
                                      &error)
                  .empty());
  EXPECT_TRUE(fs::is_empty(outside));
#endif
}

TEST(EditorSettings, OpenScriptAfterCreateRoundTrips) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#else
  const fs::path dir = WorkDir("editor_settings_roundtrip");
  const char* previous = std::getenv("XDG_CONFIG_HOME");
  const std::string previous_value = previous ? previous : "";
  const bool had_previous = previous != nullptr;
  setenv("XDG_CONFIG_HOME", dir.c_str(), 1);

  {
    EditorSettings saved;
    saved.open_script_after_create = false;
    saved.new_script_kind = Templates::ScriptKind::kCppSystem;
    saved.Save();
    EditorSettings loaded;
    loaded.Load();
    EXPECT_FALSE(loaded.open_script_after_create);
    EXPECT_EQ(loaded.new_script_kind, Templates::ScriptKind::kCppSystem);
  }

  if (had_previous) {
    setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
  } else {
    unsetenv("XDG_CONFIG_HOME");
  }
#endif
}

namespace {

bool RunProcess(const std::vector<std::string>& args, const fs::path& cwd,
                std::string* output) {
  os::Process process;
  std::string error;
  if (!process.Start(args, cwd, &error)) {
    *output += error;
    return false;
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(5);
  while (process.IsRunning()) {
    for (std::string& line : process.TakeOutput()) {
      *output += line + "\n";
    }
    if (std::chrono::steady_clock::now() > deadline) {
      process.Kill();
      *output += "timed out\n";
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  for (std::string& line : process.TakeOutput()) {
    *output += line + "\n";
  }
  return process.GetExitCode().value_or(-1) == 0;
}

}  // namespace

// The generated C++ templates must compile with the same SDK the editor uses:
// include paths, macros and the editor-system module boundary.
TEST(EditorTemplates, GeneratedCppTemplatesCompileAgainstTheDevSdk) {
  const os::EngineSdkPaths sdk = os::EngineSdkDir();
  if (!sdk.development || !fs::exists(sdk.cmake_dir / "MercuryConfig.cmake")) {
    GTEST_SKIP()
        << "no development SDK package; run the engine configure first";
  }
  const fs::path root = WorkDir("editor_template_compile");
  auto project =
      Project::Create(root / "template_compile.mproj", "Template Compile");
  ASSERT_TRUE(project);
  project->SetBuildTarget("template_compile");
  ASSERT_TRUE(project->Save());
  EditorUI::ScaffoldProject(*project);

  std::string error;
  for (const auto& [kind, name] :
       {std::pair{Templates::ScriptKind::kCppBehaviour, "PlayerController"},
        std::pair{Templates::ScriptKind::kCppSystem, "NetworkSystem"},
        std::pair{Templates::ScriptKind::kCppEditorSystem, "MyTool"}}) {
    ASSERT_FALSE(Templates::CreateScript(root, name, kind, &error).empty())
        << error;
  }

  std::string build_error;
  const fs::path build_dir =
      os::EnsureProjectBuildDir(root, "debug", &build_error);
  ASSERT_FALSE(build_dir.empty()) << build_error;
  std::string log;
  ASSERT_TRUE(RunProcess(
      cmake_build::ConfigureArguments(root, build_dir, sdk.cmake_dir), root,
      &log))
      << log;
  ASSERT_TRUE(RunProcess(
      cmake_build::BuildArguments(build_dir, "template_compile"), root, &log))
      << log;
  ASSERT_TRUE(RunProcess(
      cmake_build::BuildArguments(build_dir, project->GetBuildSystemsTarget()),
      root, &log))
      << log;
  EXPECT_TRUE(fs::is_regular_file(
      cmake_build::ProjectExecutable(build_dir, "template_compile")));
  EXPECT_TRUE(fs::is_regular_file(root / ".mercury" / "systems" /
                                  "template_compile_systems.dylib") ||
              fs::is_regular_file(root / ".mercury" / "systems" /
                                  "template_compile_systems.so"));
}

TEST(EditorSceneScripts, CppCreationSetsTargetAndRemembersSettings) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#else
  const fs::path config = WorkDir("editor_scene_script_settings");
  const char* previous = std::getenv("XDG_CONFIG_HOME");
  const std::string previous_value = previous ? previous : "";
  const bool had_previous = previous != nullptr;
  setenv("XDG_CONFIG_HOME", config.c_str(), 1);

  {
    EditorScene scene;
    const fs::path parent = WorkDir("editor_scene_scripts");
    std::string error;
    const fs::path mproj =
        Templates::CreateProject(parent, "Scene Scripts", false, &error);
    ASSERT_FALSE(mproj.empty()) << error;
    ASSERT_TRUE(scene.OpenProject(mproj));
    auto project = scene.GetProject();
    ASSERT_TRUE(project);
    EXPECT_TRUE(project->GetBuildTarget().empty());

    // open_after_create = false: nothing is handed to the external editor.
    ASSERT_TRUE(scene.CreateScript(Templates::ScriptKind::kCppSystem,
                                   "NetworkSystem", false));
    EXPECT_EQ(project->GetBuildTarget(), "scene_scripts");
    EXPECT_TRUE(
        fs::is_regular_file(project->GetRoot() / "src" / "network_system.h"));
    EXPECT_TRUE(
        fs::is_regular_file(project->GetRoot() / "src" / "network_system.cc"));
    EXPECT_EQ(scene.Settings().new_script_kind,
              Templates::ScriptKind::kCppSystem);
    EXPECT_FALSE(scene.Settings().open_script_after_create);
    // The project is not trusted, so creating C++ files must not run CMake.
    EXPECT_FALSE(fs::exists(project->GetRoot() / ".mercury" / "build" /
                            "debug" / "CMakeCache.txt"));

    EditorSettings reloaded;
    reloaded.Load();
    EXPECT_EQ(reloaded.new_script_kind, Templates::ScriptKind::kCppSystem);
    EXPECT_FALSE(reloaded.open_script_after_create);

    scene.CloseProject();
  }

  if (had_previous) {
    setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
  } else {
    unsetenv("XDG_CONFIG_HOME");
  }
#endif
}

TEST(EditorSceneScripts, TrustedProjectReconfiguresInTheBackground) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#else
  const os::EngineSdkPaths sdk = os::EngineSdkDir();
  if (!sdk.development || !fs::exists(sdk.cmake_dir / "MercuryConfig.cmake")) {
    GTEST_SKIP() << "no development SDK package";
  }
  const fs::path config = WorkDir("editor_scene_reconfigure_settings");
  const char* previous = std::getenv("XDG_CONFIG_HOME");
  const std::string previous_value = previous ? previous : "";
  const bool had_previous = previous != nullptr;
  setenv("XDG_CONFIG_HOME", config.c_str(), 1);

  {
    const fs::path parent = WorkDir("editor_scene_reconfigure");
    std::string error;
    const fs::path mproj =
        Templates::CreateProject(parent, "Reconfigure", false, &error);
    ASSERT_FALSE(mproj.empty()) << error;
    const fs::path root = mproj.parent_path();
    {
      EditorSettings settings;
      settings.SetProjectTrusted(root, true);
      settings.Save();
    }

    EditorScene scene;
    ASSERT_TRUE(scene.OpenProject(mproj));
    ASSERT_TRUE(scene.CreateScript(Templates::ScriptKind::kCppBehaviour,
                                   "PlayerController", false));

    const fs::path cache =
        root / ".mercury" / "build" / "debug" / "CMakeCache.txt";
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!fs::exists(cache) && std::chrono::steady_clock::now() < deadline) {
      scene.OnFrame(0.0);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(fs::exists(cache))
        << "the trusted project was not configured in the background";
    // The LSP link appears only after the configure this editor ran finishes.
    const fs::path link = root / "compile_commands.json";
    while (!fs::exists(link) && std::chrono::steady_clock::now() < deadline) {
      scene.OnFrame(0.0);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(fs::exists(link))
        << "the compile database was not linked after the configure";
    scene.CloseProject();
  }

  if (had_previous) {
    setenv("XDG_CONFIG_HOME", previous_value.c_str(), 1);
  } else {
    unsetenv("XDG_CONFIG_HOME");
  }
#endif
}
