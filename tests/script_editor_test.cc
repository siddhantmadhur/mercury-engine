/*
 * script_editor_test.cc
 *
 * Tests the editor-side AngelScript parser, completion engine and compiler
 * diagnostics. These do not need a wxApp.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "script_language.h"
#include "script_parser.h"

namespace Mercury::EditorUI {
namespace {

bool HasItem(const std::vector<CompletionItem>& items,
             const std::string& label) {
  return std::any_of(
      items.begin(), items.end(),
      [&](const CompletionItem& item) { return item.label == label; });
}

TEST(ScriptParserTest, IndexesClassesMethodsFieldsEnumsAndNamespaces) {
  const std::string source = R"AS(
[script]
class Ball : Behaviour
{
    [field] float speed = 60.0f;
    private int score = 0;

    void onTick(double dt)
    {
        Vector3 velocity(0.0f, 0.0f, 0.0f);
    }

    [bindable] int get_score() const property { return score; }
}

namespace Scene
{
    GameObject Find(const string &in name) { return GameObject(); }
}

enum Key { W, Up }
)AS";
  const ParsedScript parsed = ParseScript("scripts/ball.as", source);
  auto has = [&](ScriptSymbolKind kind, const std::string& name,
                 const std::string& owner = {}) {
    return std::any_of(parsed.symbols.begin(), parsed.symbols.end(),
                       [&](const ScriptSymbol& symbol) {
                         return symbol.kind == kind && symbol.name == name &&
                                (owner.empty() || symbol.owner == owner);
                       });
  };
  EXPECT_TRUE(has(ScriptSymbolKind::kClass, "Ball"));
  EXPECT_TRUE(has(ScriptSymbolKind::kMethod, "onTick", "Ball"));
  EXPECT_TRUE(has(ScriptSymbolKind::kField, "speed", "Ball"));
  EXPECT_TRUE(has(ScriptSymbolKind::kField, "score", "Ball"));
  EXPECT_TRUE(has(ScriptSymbolKind::kFunction, "Find", "Scene"));
  EXPECT_TRUE(has(ScriptSymbolKind::kEnum, "Key"));
  EXPECT_TRUE(has(ScriptSymbolKind::kEnumValue, "W", "Key"));
}

TEST(ScriptParserTest, CompletesMembersOfNativeAndScriptTypes) {
  const std::string source = R"AS(
[script]
class Ball : Behaviour
{
    GameObject self;
    void onStart()
    {
        GameManager@ manager = cast<GameManager>(null);
        manager.
        self.
    }
}
)AS";
  ScriptWorkspace workspace;
  workspace.SetFile(
      "scripts/game_manager.as",
      "class GameManager { bool IsPaused() { return false; } }\n");
  const std::string text = R"AS(
[script]
class Ball : Behaviour
{
    GameObject self;
    void onStart()
    {
        GameManager@ manager = cast<GameManager>(null);
        manager.
        self.
    }
}
)AS";
  const size_t manager_dot = text.find("manager.") + 8;
  const auto manager_items =
      CompleteAt(workspace, "scripts/ball.as", text, manager_dot, {});
  EXPECT_TRUE(HasItem(manager_items, "IsPaused"));

  ScriptSymbol native;
  native.kind = ScriptSymbolKind::kMethod;
  native.name = "get_position";
  native.owner = "GameObject";
  native.type = "Vector3";
  native.signature = "Vector3 get_position() const property";
  ScriptSymbol position;
  position.kind = ScriptSymbolKind::kField;
  position.name = "position";
  position.owner = "GameObject";
  position.type = "Vector3";
  const size_t self_dot = text.find("self.") + 5;
  const auto self_items = CompleteAt(workspace, "scripts/ball.as", text,
                                     self_dot, {native, position});
  EXPECT_TRUE(HasItem(self_items, "position"));
}

TEST(ScriptParserTest, CompletesNamespacesAndEnums) {
  const std::string source = R"AS(
class Ball : Behaviour
{
    void onStart()
    {
        Scene::
        Key::
    }
}
)AS";
  const ParsedScript parsed = ParseScript("scripts/ball.as", source);
  EXPECT_EQ(parsed.classes.size(), 1u);

  ScriptSymbol find;
  find.kind = ScriptSymbolKind::kFunction;
  find.name = "Find";
  find.owner = "Scene";
  find.signature = "GameObject Find(const string &in)";
  ScriptSymbol key_w;
  key_w.kind = ScriptSymbolKind::kEnumValue;
  key_w.name = "W";
  key_w.owner = "Key";
  const size_t scene_colons = source.find("Scene::") + 7;
  const auto scene_items =
      CompleteAt({}, "scripts/ball.as", source, scene_colons, {find, key_w});
  EXPECT_TRUE(HasItem(scene_items, "Find"));
  const size_t key_colons = source.find("Key::") + 5;
  const auto key_items =
      CompleteAt({}, "scripts/ball.as", source, key_colons, {find, key_w});
  EXPECT_TRUE(HasItem(key_items, "W"));
}

TEST(ScriptParserTest, ParsesExampleScripts) {
  const std::string path = std::string(MERCURY_SOURCE_DIR) +
                           "/examples/roll_a_ball/scripts/player_controller.as";
  std::ifstream in(path, std::ios::binary);
  ASSERT_TRUE(in.good()) << path;
  std::ostringstream text;
  text << in.rdbuf();
  const ParsedScript parsed =
      ParseScript("scripts/player_controller.as", text.str());
  ASSERT_FALSE(parsed.classes.empty());
  EXPECT_EQ(parsed.classes.front().name, "PlayerController");
  EXPECT_TRUE(std::any_of(parsed.symbols.begin(), parsed.symbols.end(),
                          [](const ScriptSymbol& symbol) {
                            return symbol.name == "onTick" &&
                                   symbol.kind == ScriptSymbolKind::kMethod;
                          }));
}

TEST(ScriptLanguageTest, IndexesNativeApi) {
  ScriptLanguageService service;
  ASSERT_TRUE(service.Initialize()) << service.Error();
  const auto& symbols = service.NativeSymbols();
  auto has = [&](const std::string& name, const std::string& owner) {
    return std::any_of(symbols.begin(), symbols.end(),
                       [&](const ScriptSymbol& symbol) {
                         return symbol.name == name && symbol.owner == owner;
                       });
  };
  EXPECT_TRUE(has("Find", "Scene"));
  EXPECT_TRUE(has("IsKeyPressed", "Input"));
  EXPECT_TRUE(has("position", "GameObject"));
  EXPECT_TRUE(has("W", "Key"));
}

TEST(ScriptLanguageTest, CompilesExampleProject) {
  ScriptLanguageService service;
  ASSERT_TRUE(service.Initialize()) << service.Error();
  const std::filesystem::path root =
      std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/roll_a_ball";
  std::vector<std::pair<std::string, std::string>> sections;
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root / "scripts", ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".as") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    sections.emplace_back(
        std::filesystem::relative(entry.path(), root, ec).generic_string(),
        text.str());
  }
  ASSERT_FALSE(sections.empty());
  std::vector<ScriptDiagnostic> diagnostics;
  ASSERT_TRUE(service.Check(sections, &diagnostics));
  for (const ScriptDiagnostic& diagnostic : diagnostics) {
    ADD_FAILURE() << diagnostic.file << ":" << diagnostic.line << ": "
                  << diagnostic.message;
  }
}

TEST(ScriptLanguageTest, ReportsCompilerErrors) {
  ScriptLanguageService service;
  ASSERT_TRUE(service.Initialize()) << service.Error();
  std::vector<ScriptDiagnostic> diagnostics;
  const std::string bad = R"AS(
class Broken : Behaviour
{
    void onStart()
    {
        ThisFunctionDoesNotExist();
    }
}
)AS";
  ASSERT_TRUE(service.Check({{"scripts/broken.as", bad}}, &diagnostics));
  EXPECT_FALSE(diagnostics.empty());
  EXPECT_EQ(diagnostics.front().file, "scripts/broken.as");
  EXPECT_GT(diagnostics.front().line, 0);
}

}  // namespace
}  // namespace Mercury::EditorUI
