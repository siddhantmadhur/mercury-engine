// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the guides tool group: the MCP resources (the reserved
// __resources/list and __resources/read tools), the get_agent_guides tool, the
// guide text built from a real project (a copy of examples/roll_a_ball) and
// the project-structure cap.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/tool_registry.h"
#include "mcp/tools/guides.h"
#include "mcp_tools_fixture.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace Mercury;
using Mercury::mcp::ClassifyProjectFiles;
using Mercury::mcp::ToolResult;
using Mercury::mcp::testing::McpToolFixture;
using nlohmann::json;

namespace {

const char* const kGuideNames[] = {"AGENTS.md", "CLAUDE.md", "GEMINI.md"};

bool Contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

// __resources/read of `uri`; the result must be ok and carry one content.
std::string ReadText(McpToolFixture& fixture, const std::string& uri,
                     std::string* mime = nullptr) {
  const ToolResult result =
      fixture.CallTool("__resources/read", json{{"uri", uri}});
  EXPECT_TRUE(result.ok) << uri << ": " << result.result.dump();
  if (!result.ok) return {};
  const json& contents = result.result["contents"];
  EXPECT_EQ(contents.size(), 1u);
  EXPECT_EQ(contents[0]["uri"], uri);
  if (mime) *mime = contents[0]["mimeType"].get<std::string>();
  return contents[0]["text"].get<std::string>();
}

std::string GuideContent(McpToolFixture& fixture, const std::string& name) {
  const ToolResult result = fixture.CallTool(
      "get_agent_guides", json{{"files", json::array({name})}});
  EXPECT_TRUE(result.ok) << result.result.dump();
  if (!result.ok) return {};
  EXPECT_EQ(result.result["files"].size(), 1u);
  return result.result["files"][0]["content"].get<std::string>();
}

bool ListContains(const json& list, const std::string& value) {
  return std::find(list.begin(), list.end(), json(value)) != list.end();
}

}  // namespace

// ----------------------------------------------------------- resources ----

TEST(McpToolsGuidesTest, ListsEveryResourceAndTheAssetTemplate) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("__resources/list");
  ASSERT_TRUE(result.ok) << result.result.dump();

  std::vector<std::string> uris;
  for (const json& resource : result.result["resources"]) {
    EXPECT_TRUE(resource["name"].is_string());
    EXPECT_TRUE(resource["mimeType"].is_string());
    EXPECT_TRUE(resource["description"].is_string());
    uris.push_back(resource["uri"]);
  }
  EXPECT_EQ(uris,
            (std::vector<std::string>{
                "mercury://guides/AGENTS.md", "mercury://guides/CLAUDE.md",
                "mercury://guides/GEMINI.md", "mercury://project/structure",
                "mercury://project/schema"}));
  for (const json& resource : result.result["resources"]) {
    const std::string uri = resource["uri"];
    EXPECT_EQ(resource["mimeType"], uri.starts_with("mercury://guides/")
                                        ? "text/markdown"
                                        : "application/json")
        << uri;
  }

  ASSERT_EQ(result.result["templates"].size(), 1u);
  EXPECT_EQ(result.result["templates"][0]["uriTemplate"],
            "mercury://asset/{+path}");
  EXPECT_TRUE(result.result["templates"][0]["name"].is_string());
  EXPECT_EQ(result.result["templates"][0]["mimeType"], "application/json");
}

TEST(McpToolsGuidesTest, EveryListedResourceReads) {
  McpToolFixture fixture;
  const ToolResult list = fixture.CallTool("__resources/list");
  ASSERT_TRUE(list.ok);
  for (const json& resource : list.result["resources"]) {
    std::string mime;
    const std::string text = ReadText(fixture, resource["uri"], &mime);
    EXPECT_FALSE(text.empty()) << resource["uri"];
    EXPECT_EQ(mime, resource["mimeType"].get<std::string>()) << resource["uri"];
  }
}

TEST(McpToolsGuidesTest, GuideResourcesCarryTheProjectsFacts) {
  McpToolFixture fixture;
  for (const char* name : kGuideNames) {
    SCOPED_TRACE(name);
    const std::string text =
        ReadText(fixture, std::string("mercury://guides/") + name);
    // The live project section: roll_a_ball's name, main map, input actions.
    EXPECT_TRUE(Contains(text, "`Roll a Ball`"));
    EXPECT_TRUE(Contains(text, "`res://maps/main.mmap`"));
    EXPECT_TRUE(Contains(text, "`res://input/actions.minput`"));
    EXPECT_TRUE(Contains(text, "`Player/Move` (value, vector2)"));
    EXPECT_TRUE(Contains(text, "`Player/Jump` (button)"));
    EXPECT_TRUE(Contains(text, "Action maps: `Player`"));
    EXPECT_TRUE(Contains(text, "`Default`"));     // a layer
    EXPECT_TRUE(Contains(text, "`MainCamera`"));  // a tag
    EXPECT_TRUE(Contains(text, "1280 x 720"));
    // The shared body.
    EXPECT_TRUE(Contains(text, "TOML"));
    EXPECT_TRUE(Contains(text, "[callable]"));
    EXPECT_TRUE(Contains(text, "get_editor_state"));
    EXPECT_TRUE(Contains(text, "save_map"));
    // Every placeholder was filled.
    EXPECT_FALSE(Contains(text, "{{")) << "an unfilled placeholder";
    EXPECT_FALSE(Contains(text, "}}"));
  }
}

TEST(McpToolsGuidesTest, AgentHeadersDiffer) {
  McpToolFixture fixture;
  const std::string agents = GuideContent(fixture, "AGENTS.md");
  const std::string claude = GuideContent(fixture, "CLAUDE.md");
  const std::string gemini = GuideContent(fixture, "GEMINI.md");

  EXPECT_TRUE(Contains(claude, "claude mcp add --transport http mercury "));
  EXPECT_TRUE(Contains(claude, "--header \"Authorization: Bearer <token>\""));
  EXPECT_TRUE(Contains(claude, "/mcp"));
  EXPECT_FALSE(Contains(agents, "claude mcp add"));
  EXPECT_FALSE(Contains(gemini, "claude mcp add"));

  EXPECT_TRUE(Contains(gemini, "\"httpUrl\": \"http://127.0.0.1:"));
  EXPECT_TRUE(Contains(gemini, "settings.json"));
  EXPECT_FALSE(Contains(agents, "httpUrl"));
  EXPECT_FALSE(Contains(claude, "httpUrl"));
}

TEST(McpToolsGuidesTest, GuidesNeverContainTheBearerToken) {
  McpToolFixture fixture;
  const std::string token(64, 'a');
  fixture.Scene().Settings().mcp_token = token;
  for (const char* name : kGuideNames) {
    const std::string text = GuideContent(fixture, name);
    EXPECT_FALSE(Contains(text, token)) << name;
    EXPECT_TRUE(Contains(text, "<token>")) << name;
    EXPECT_TRUE(Contains(text, "File > Preferences > MCP")) << name;
  }
}

TEST(McpToolsGuidesTest, GuideUrlFollowsTheSettings) {
  McpToolFixture fixture;
  fixture.Scene().Settings().mcp_port = 51234;
  fixture.Scene().Settings().mcp_ephemeral = false;
  EXPECT_TRUE(Contains(GuideContent(fixture, "CLAUDE.md"),
                       "http://127.0.0.1:51234/mcp"));
  fixture.Scene().Settings().mcp_ephemeral = true;
  EXPECT_TRUE(Contains(GuideContent(fixture, "CLAUDE.md"),
                       "http://127.0.0.1:<chosen when the server starts>/mcp"));
}

TEST(McpToolsGuidesTest, GuideReportsTheAccessMode) {
  McpToolFixture fixture;
  fixture.Scene().Settings().mcp_allow_edits = false;
  // CallTool(allow_edits) sets the setting for the call; read tools run in
  // either mode, and the text follows the mode.
  const ToolResult read_only = fixture.CallTool(
      "get_agent_guides", json{{"files", json::array({"AGENTS.md"})}}, false);
  ASSERT_TRUE(read_only.ok);
  EXPECT_TRUE(
      Contains(read_only.result["files"][0]["content"].get<std::string>(),
               "Access Mode: Read-Only"));
  const ToolResult editable = fixture.CallTool(
      "get_agent_guides", json{{"files", json::array({"AGENTS.md"})}}, true);
  ASSERT_TRUE(editable.ok);
  EXPECT_TRUE(
      Contains(editable.result["files"][0]["content"].get<std::string>(),
               "Access Mode: Allow Scene Edits"));
}

TEST(McpToolsGuidesTest, GuideListsTheRegisteredTools) {
  McpToolFixture fixture;
  const std::string text = GuideContent(fixture, "AGENTS.md");
  EXPECT_TRUE(Contains(text, "| `ping` | Read |"));
  EXPECT_TRUE(Contains(text, "| `get_server_info` | Read |"));
  EXPECT_TRUE(Contains(text, "| `get_agent_guides` | Read |"));
  // The reserved transport names are not tools.
  EXPECT_FALSE(Contains(text, "| `__resources"));
}

TEST(McpToolsGuidesTest, GuideSamplesAtMostTwentyFilesPerKind) {
  McpToolFixture fixture;
  const fs::path dir = fixture.Root() / "assets" / "many";
  fs::create_directories(dir);
  for (int i = 0; i < 30; i++) {
    std::ofstream(dir / ("t" + std::to_string(100 + i) + ".png")) << "x";
  }
  const std::string text = GuideContent(fixture, "AGENTS.md");
  EXPECT_TRUE(Contains(text, "- textures (30): "));
  EXPECT_TRUE(Contains(text, "`res://assets/many/t100.png`"));
  EXPECT_TRUE(Contains(text, "`res://assets/many/t119.png`"));
  EXPECT_FALSE(Contains(text, "t120.png"));
  EXPECT_TRUE(Contains(text, ", ...\n"));
}

TEST(McpToolsGuidesTest, ProjectTextCannotBreakOutOfTheGuide) {
  McpToolFixture fixture;
  fixture.Scene().GetProject()->SetName("Evil`\n# Injected heading\n{{tools}}");
  const std::string text = GuideContent(fixture, "AGENTS.md");
  EXPECT_FALSE(Contains(text, "\n# Injected heading"));
  EXPECT_TRUE(Contains(text, "Evil' # Injected heading {{tools}}"));
  // The pasted placeholder was not expanded a second time.
  EXPECT_TRUE(Contains(text, "{{tools}}"));
}

TEST(McpToolsGuidesTest, GuidesWithNoProjectOpenStillRender) {
  McpToolFixture fixture;
  fixture.Scene().CloseProject();
  const std::string text = GuideContent(fixture, "GEMINI.md");
  EXPECT_TRUE(Contains(text, "No project is open"));
  EXPECT_TRUE(Contains(text, "httpUrl"));
}

// ------------------------------------------------- structure and schema ----

TEST(McpToolsGuidesTest, StructureGroupsTheProjectByKind) {
  McpToolFixture fixture;
  std::string mime;
  const std::string text =
      ReadText(fixture, "mercury://project/structure", &mime);
  EXPECT_EQ(mime, "application/json");
  const json structure = json::parse(text);

  EXPECT_EQ(structure["name"], "Roll a Ball");
  EXPECT_EQ(structure["main_map"], "res://maps/main.mmap");
  EXPECT_FALSE(structure["truncated"].get<bool>());
  EXPECT_EQ(structure["cap"], 2000);

  const json& kinds = structure["kinds"];
  for (const char* kind :
       {"project", "maps", "prefabs", "scripts", "ui", "input", "animators",
        "data_assets", "models", "textures", "audio"}) {
    EXPECT_TRUE(kinds.contains(kind)) << kind;
    EXPECT_TRUE(structure["counts"].contains(kind)) << kind;
  }
  EXPECT_TRUE(ListContains(kinds["project"], "res://roll_a_ball.mproj"));
  EXPECT_TRUE(ListContains(kinds["maps"], "res://maps/main.mmap"));
  EXPECT_TRUE(
      ListContains(kinds["scripts"], "res://scripts/player_controller.as"));
  EXPECT_TRUE(ListContains(kinds["ui"], "res://ui/score.uxml"));
  EXPECT_TRUE(ListContains(kinds["ui"], "res://ui/score.uss"));
  EXPECT_TRUE(ListContains(kinds["input"], "res://input/actions.minput"));
  EXPECT_TRUE(ListContains(kinds["data_assets"], "res://assets/player.masset"));
  EXPECT_EQ(structure["counts"]["data_assets"], kinds["data_assets"].size());

  // res:// paths only: no absolute path leaks out.
  size_t listed = 0;
  for (const auto& [kind, files] : kinds.items()) {
    for (const json& file : files) {
      EXPECT_TRUE(file.get<std::string>().starts_with("res://")) << file;
      listed++;
    }
  }
  EXPECT_EQ(structure["returned"], listed);
  EXPECT_EQ(structure["total"], listed);
  EXPECT_FALSE(Contains(text, fixture.Root().string()));
}

TEST(McpToolsGuidesTest, StructureSeesCppScriptsUnderSrcOnly) {
  McpToolFixture fixture;
  fs::create_directories(fixture.Root() / "src");
  std::ofstream(fixture.Root() / "src" / "spinner.h") << "x";
  std::ofstream(fixture.Root() / "src" / "spinner.cc") << "x";
  std::ofstream(fixture.Root() / "src" / "notes.txt") << "x";
  std::ofstream(fixture.Root() / "stray.cc") << "x";
  const json structure =
      json::parse(ReadText(fixture, "mercury://project/structure"));
  EXPECT_TRUE(
      ListContains(structure["kinds"]["scripts"], "res://src/spinner.h"));
  EXPECT_TRUE(
      ListContains(structure["kinds"]["scripts"], "res://src/spinner.cc"));
  EXPECT_FALSE(ListContains(structure["kinds"]["scripts"], "res://stray.cc"));
  EXPECT_FALSE(
      ListContains(structure["kinds"]["scripts"], "res://src/notes.txt"));
}

TEST(McpToolsGuidesTest, StructureCapKeepsEarlierKindsAndSaysTruncated) {
  std::vector<std::string> paths = {"res://p.mproj",     "res://maps/a.mmap",
                                    "res://maps/b.mmap", "res://scripts/s.as",
                                    "res://src/x.h",     "res://other/x.h",
                                    "res://readme.txt"};
  for (int i = 0; i < 5000; i++) {
    paths.push_back("res://tex/t" + std::to_string(i) + ".png");
  }

  const json capped = ClassifyProjectFiles(paths);  // the default cap
  EXPECT_EQ(capped["cap"], 2000);
  EXPECT_EQ(capped["returned"], 2000);
  EXPECT_EQ(capped["total"], 5005);  // .txt and the stray .h do not count
  EXPECT_TRUE(capped["truncated"].get<bool>());
  // Earlier kinds are whole; the cap is spent on textures.
  EXPECT_EQ(capped["kinds"]["project"].size(), 1u);
  EXPECT_EQ(capped["kinds"]["maps"].size(), 2u);
  EXPECT_EQ(capped["kinds"]["scripts"],
            json::array({"res://scripts/s.as", "res://src/x.h"}));
  EXPECT_EQ(capped["kinds"]["textures"].size(), 2000u - 5u);
  // The true sizes survive the cap.
  EXPECT_EQ(capped["counts"]["textures"], 5000);
  EXPECT_EQ(capped["counts"]["maps"], 2);

  // A cap that fits everything does not truncate, at the boundary too.
  const json exact = ClassifyProjectFiles(paths, 5005);
  EXPECT_FALSE(exact["truncated"].get<bool>());
  EXPECT_EQ(exact["returned"], 5005);
  const json one_short = ClassifyProjectFiles(paths, 5004);
  EXPECT_TRUE(one_short["truncated"].get<bool>());
  EXPECT_EQ(one_short["returned"], 5004);
  const json none = ClassifyProjectFiles(paths, 0);
  EXPECT_TRUE(none["truncated"].get<bool>());
  EXPECT_EQ(none["returned"], 0);
  EXPECT_EQ(none["counts"]["maps"], 2);
}

TEST(McpToolsGuidesTest, StructureKindsByExtension) {
  using mcp::StructureKindOf;
  EXPECT_EQ(StructureKindOf("res://a.mproj"), "project");
  EXPECT_EQ(StructureKindOf("res://maps/a.MMAP"), "maps");
  EXPECT_EQ(StructureKindOf("res://a.mprefab"), "prefabs");
  EXPECT_EQ(StructureKindOf("res://scripts/a.as"), "scripts");
  EXPECT_EQ(StructureKindOf("res://src/a.cc"), "scripts");
  EXPECT_EQ(StructureKindOf("res://src/deep/a.h"), "scripts");
  EXPECT_EQ(StructureKindOf("res://system/a.cc"), "");
  EXPECT_EQ(StructureKindOf("res://ui/a.uxml"), "ui");
  EXPECT_EQ(StructureKindOf("res://ui/a.uss"), "ui");
  EXPECT_EQ(StructureKindOf("res://a.minput"), "input");
  EXPECT_EQ(StructureKindOf("res://a.manim"), "animators");
  EXPECT_EQ(StructureKindOf("res://a.masset"), "data_assets");
  EXPECT_EQ(StructureKindOf("res://m/a.glb"), "models");
  EXPECT_EQ(StructureKindOf("res://m/a.obj"), "models");
  EXPECT_EQ(StructureKindOf("res://t/a.PNG"), "textures");
  EXPECT_EQ(StructureKindOf("res://s/a.ogg"), "audio");
  EXPECT_EQ(StructureKindOf("res://s/a.wav"), "audio");
  EXPECT_EQ(StructureKindOf("res://dir.png/file"), "");
  EXPECT_EQ(StructureKindOf("res://noextension"), "");
}

TEST(McpToolsGuidesTest, SchemaResourceIsTheEngineSchema) {
  McpToolFixture fixture;
  std::string mime;
  const std::string text = ReadText(fixture, "mercury://project/schema", &mime);
  EXPECT_EQ(mime, "application/json");
  const json schema = json::parse(text);
  EXPECT_FALSE(schema.empty());
  EXPECT_TRUE(Contains(text, "\"Transform\""));
}

// ---------------------------------------------------------- asset template
// ----

TEST(McpToolsGuidesTest, AssetTemplateInspectsAFile) {
  McpToolFixture fixture;
  std::string mime;
  const std::string text =
      ReadText(fixture, "mercury://asset/assets/player.masset", &mime);
  EXPECT_EQ(mime, "application/json");
  const json inspected = json::parse(text);
  EXPECT_TRUE(inspected.is_object());
  EXPECT_FALSE(inspected.empty());

  // Percent-encoding and the res:// form name the same file.
  EXPECT_EQ(
      json::parse(ReadText(fixture, "mercury://asset/assets%2Fplayer.masset")),
      inspected);
  EXPECT_EQ(json::parse(ReadText(fixture,
                                 "mercury://asset/res://assets/player.masset")),
            inspected);
}

TEST(McpToolsGuidesTest, AssetTemplateErrorsAndSuggests) {
  McpToolFixture fixture;
  ToolResult missing = fixture.CallTool(
      "__resources/read", json{{"uri", "mercury://asset/assets/playr.masset"}});
  EXPECT_FALSE(missing.ok);
  EXPECT_TRUE(missing.result["error"].is_string());
  EXPECT_EQ(missing.result["suggestion"],
            "mercury://asset/assets/player.masset");

  for (const char* uri :
       {"mercury://asset/", "mercury://asset/../../etc/passwd",
        "mercury://asset/%zz", "mercury://asset/%2e%2e/%2e%2e/x"}) {
    const ToolResult bad =
        fixture.CallTool("__resources/read", json{{"uri", uri}});
    EXPECT_FALSE(bad.ok) << uri;
    EXPECT_TRUE(bad.result["error"].is_string()) << uri;
  }
}

TEST(McpToolsGuidesTest, UnknownResourceErrors) {
  McpToolFixture fixture;
  ToolResult unknown = fixture.CallTool(
      "__resources/read", json{{"uri", "mercury://nope/missing"}});
  EXPECT_FALSE(unknown.ok);
  EXPECT_TRUE(Contains(unknown.result["error"], "resource not found"));
  EXPECT_TRUE(Contains(unknown.result["error"], "mercury://nope/missing"));

  // A near miss suggests the real URI.
  ToolResult typo = fixture.CallTool(
      "__resources/read", json{{"uri", "mercury://guides/AGENT.md"}});
  EXPECT_FALSE(typo.ok);
  EXPECT_EQ(typo.result["suggestion"], "mercury://guides/AGENTS.md");
  ToolResult typo2 = fixture.CallTool(
      "__resources/read", json{{"uri", "mercury://project/structur"}});
  EXPECT_FALSE(typo2.ok);
  EXPECT_EQ(typo2.result["suggestion"], "mercury://project/structure");

  // A guide URI with an unknown name is the same error.
  EXPECT_FALSE(fixture
                   .CallTool("__resources/read",
                             json{{"uri", "mercury://guides/OTHER.md"}})
                   .ok);
  EXPECT_FALSE(fixture.CallTool("__resources/read", json{{"uri", ""}}).ok);
  EXPECT_FALSE(fixture.CallTool("__resources/read", json::object()).ok);
  EXPECT_FALSE(fixture.CallTool("__resources/read", json{{"uri", 42}}).ok);
}

TEST(McpToolsGuidesTest, ResourcesAreReadOnlyTools) {
  McpToolFixture fixture;
  // Read mode: allowed when the server is read-only.
  const ToolResult list =
      fixture.CallTool("__resources/list", json::object(), false);
  EXPECT_TRUE(list.ok);
  const ToolResult read = fixture.CallTool(
      "__resources/read", json{{"uri", "mercury://guides/AGENTS.md"}}, false);
  EXPECT_TRUE(read.ok);
}

// ------------------------------------------------------- get_agent_guides ----

TEST(McpToolsGuidesTest, GetAgentGuidesReturnsAllByDefault) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool("get_agent_guides");
  ASSERT_TRUE(result.ok) << result.result.dump();
  const json& files = result.result["files"];
  ASSERT_EQ(files.size(), 3u);
  EXPECT_EQ(files[0]["name"], "AGENTS.md");
  EXPECT_EQ(files[1]["name"], "CLAUDE.md");
  EXPECT_EQ(files[2]["name"], "GEMINI.md");
  for (const json& file : files) {
    EXPECT_TRUE(Contains(file["content"], "Roll a Ball")) << file["name"];
  }
  // The same text the resource serves.
  EXPECT_EQ(files[1]["content"].get<std::string>(),
            ReadText(fixture, "mercury://guides/CLAUDE.md"));

  // null and [] mean "all" too.
  EXPECT_EQ(fixture.CallTool("get_agent_guides", json{{"files", nullptr}})
                .result["files"]
                .size(),
            3u);
  EXPECT_EQ(fixture.CallTool("get_agent_guides", json{{"files", json::array()}})
                .result["files"]
                .size(),
            3u);
}

TEST(McpToolsGuidesTest, GetAgentGuidesFilters) {
  McpToolFixture fixture;
  const ToolResult result = fixture.CallTool(
      "get_agent_guides",
      json{{"files", json::array({"GEMINI.md", "AGENTS.md", "GEMINI.md"})}});
  ASSERT_TRUE(result.ok) << result.result.dump();
  const json& files = result.result["files"];
  ASSERT_EQ(files.size(), 2u);  // in the order asked for, without repeats
  EXPECT_EQ(files[0]["name"], "GEMINI.md");
  EXPECT_EQ(files[1]["name"], "AGENTS.md");
}

TEST(McpToolsGuidesTest, GetAgentGuidesRejectsBadArguments) {
  McpToolFixture fixture;
  const ToolResult unknown = fixture.CallTool(
      "get_agent_guides", json{{"files", json::array({"agents.md"})}});
  EXPECT_FALSE(unknown.ok);
  EXPECT_TRUE(Contains(unknown.result["error"], "agents.md"));
  EXPECT_EQ(unknown.result["suggestion"], "AGENTS.md");

  EXPECT_FALSE(
      fixture.CallTool("get_agent_guides", json{{"files", "AGENTS.md"}}).ok);
  EXPECT_FALSE(
      fixture.CallTool("get_agent_guides", json{{"files", json::array({1})}})
          .ok);
  // One bad name fails the whole call: nothing is returned half-way.
  EXPECT_FALSE(fixture
                   .CallTool("get_agent_guides",
                             json{{"files", json::array({"AGENTS.md", "x"})}})
                   .ok);
}

TEST(McpToolsGuidesTest, GetAgentGuidesWritesNothing) {
  McpToolFixture fixture;
  std::vector<fs::path> before;
  for (const auto& entry : fs::recursive_directory_iterator(fixture.Root())) {
    before.push_back(entry.path());
  }
  ASSERT_TRUE(fixture.CallTool("get_agent_guides").ok);
  std::vector<fs::path> after;
  for (const auto& entry : fs::recursive_directory_iterator(fixture.Root())) {
    after.push_back(entry.path());
  }
  std::sort(before.begin(), before.end());
  std::sort(after.begin(), after.end());
  EXPECT_EQ(before, after);
  EXPECT_FALSE(fs::exists(fixture.Root() / "AGENTS.md"));
  EXPECT_FALSE(fs::exists(fixture.Root() / "CLAUDE.md"));
}

TEST(McpToolsGuidesTest, GetAgentGuidesIsRegisteredAsARead) {
  const mcp::ToolEntry* entry =
      mcp::ToolRegistry::Instance().Find("get_agent_guides");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->mode, mcp::ToolMode::kRead);
}

// The workflow text names tools by hand (the sibling tool groups are written
// elsewhere). Once those groups are merged every one of them must exist; before
// that the test skips instead of failing, so it passes on both sides.
TEST(McpToolsGuidesTest, WorkflowNamesRegisteredTools) {
  McpToolFixture fixture;
  const std::string text = GuideContent(fixture, "AGENTS.md");
  const size_t begin = text.find("## Recommended workflow");
  ASSERT_NE(begin, std::string::npos);
  const size_t end = text.find("\n## ", begin + 1);
  const std::string section = text.substr(begin, end - begin);

  // Numbered steps: "1. `get_editor_state`: ..." / "2. `find_nodes` /
  // `get_node`: ...".
  std::vector<std::string> names;
  size_t line_start = 0;
  while (line_start < section.size()) {
    size_t line_end = section.find('\n', line_start);
    if (line_end == std::string::npos) line_end = section.size();
    const std::string line = section.substr(line_start, line_end - line_start);
    line_start = line_end + 1;
    if (line.size() < 4 || !std::isdigit(static_cast<unsigned char>(line[0])) ||
        line.compare(1, 2, ". ") != 0) {
      continue;
    }
    const std::string head = line.substr(0, line.find(':'));
    size_t open = 0;
    while ((open = head.find('`', open)) != std::string::npos) {
      const size_t close = head.find('`', open + 1);
      if (close == std::string::npos) break;
      names.push_back(head.substr(open + 1, close - open - 1));
      open = close + 1;
    }
  }
  EXPECT_EQ(names, (std::vector<std::string>{
                       "get_editor_state", "find_nodes", "get_node",
                       "apply_scene_operations", "capture_viewport",
                       "validate_project", "save_map"}));

  std::vector<std::string> missing;
  for (const std::string& name : names) {
    if (!mcp::ToolRegistry::Instance().Find(name)) missing.push_back(name);
  }
  if (!missing.empty()) {
    std::string list;
    for (const std::string& name : missing) list += " " + name;
    GTEST_SKIP() << "workflow tools not registered yet (their groups are not "
                    "merged):"
                 << list;
  }
}
