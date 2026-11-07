// Tests of the editor's TypeScript pieces: Create Script, new projects and the Console's
// clickable script positions. Links the editor library, so it is a separate executable.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "console.h"
#include "scripting/js/script_builder.h"
#include "scripting/ts_types.h"
#include "templates.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {
fs::path WorkDir(const char* name) {
  const fs::path dir = fs::path(MERCURY_TEST_CACHE_DIR) / name;
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

TEST(EditorTemplates, NewProjectHasATsconfigAndNoCMake) {
  const fs::path parent = WorkDir("editor_new_project");
  std::string error;
  const fs::path mproj = Templates::CreateProject(parent, "My Game", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();
  EXPECT_TRUE(fs::exists(root / "tsconfig.json"));
  EXPECT_TRUE(fs::is_directory(root / "scripts"));
  EXPECT_FALSE(fs::exists(root / "CMakeLists.txt"));
  EXPECT_FALSE(fs::exists(root / ".clangd"));
  EXPECT_NE(Read(root / ".gitignore").find(".mercury/"), std::string::npos);
  EXPECT_EQ(Read(root / "tsconfig.json").find("experimentalDecorators"), std::string::npos);
}

TEST(EditorTemplates, CreateScriptWritesATypeScriptClassThatBuilds) {
  const fs::path parent = WorkDir("editor_create_script");
  std::string error;
  const fs::path mproj = Templates::CreateProject(parent, "Demo", true, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  const fs::path root = mproj.parent_path();

  const fs::path script = Templates::CreateScript(root, "PlayerMover", &error);
  ASSERT_FALSE(script.empty()) << error;
  EXPECT_EQ(script.filename(), "player_mover.ts");
  const std::string text = Read(script);
  EXPECT_NE(text.find("@script()"), std::string::npos);
  EXPECT_NE(text.find("class PlayerMover extends Behaviour"), std::string::npos);
  EXPECT_NE(text.find("@field() speed = 1;"), std::string::npos);

  // A second script with the same name is refused, and a bad name too.
  EXPECT_TRUE(Templates::CreateScript(root, "PlayerMover", &error).empty());
  EXPECT_TRUE(Templates::CreateScript(root, "not valid", &error).empty());
  EXPECT_NE(error.find("TypeScript class name"), std::string::npos);

  // The template builds with esbuild.
  std::string log;
  EXPECT_TRUE(ScriptBuilder::Build(root, {}, &log)) << log;
  EXPECT_TRUE(fs::exists(ScriptBuilder::BundlePath(root)));
}

TEST(EditorConsole, ScriptPositionsAreClickable) {
  EXPECT_EQ(Console::FindScriptFile("Ball.onTick: Error: boom\n    at onTick (scripts/ball.ts:42:5)"),
            std::optional<std::string>("scripts/ball.ts"));
  EXPECT_EQ(Console::FindScriptFile("scripts/sub/ball.ts(12,3): error TS2304: Cannot find name"),
            std::optional<std::string>("scripts/sub/ball.ts"));
  EXPECT_EQ(Console::FindScriptFile("    at get name (../types/mercury/gameobject.ts:122:40)"), std::nullopt);
  EXPECT_EQ(Console::FindScriptFile("saved scripts/ folder"), std::nullopt);
  EXPECT_EQ(Console::FindScriptFile("scripts/ball.ts is fine"), std::nullopt);
}
