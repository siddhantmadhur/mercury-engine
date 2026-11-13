// Tests of the editor's AngelScript pieces: Create Script, new projects and the Console's
// clickable script positions. Links the editor library, so it is a separate executable.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "console.h"
#include "editor_scene.h"
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
}  // namespace

TEST(EditorTemplates, NewProjectHasScriptsAndNoCMake) {
  const fs::path parent = WorkDir("editor_new_project");
  std::string error;
  const fs::path mproj = Templates::CreateProject(parent, "My Game", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();
  EXPECT_TRUE(fs::is_directory(root / "scripts"));
  EXPECT_FALSE(fs::exists(root / "CMakeLists.txt"));
  EXPECT_FALSE(fs::exists(root / ".clangd"));
  EXPECT_NE(Read(root / ".gitignore").find(".mercury/"), std::string::npos);
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
