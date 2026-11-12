// Tests of the editor's AngelScript pieces: Create Script, new projects and the Console's
// clickable script positions. Links the editor library, so it is a separate executable.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "console.h"
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
