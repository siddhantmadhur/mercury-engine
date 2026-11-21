// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the editor's MCP host: the tool registry (dispatch, unknown
// tools, the mode re-check), the .mcp.json merge/remove rules, the
// disabled-means-no-server rule and, on POSIX, an ephemeral server that
// really listens and really stops. The Rust transport's call_tool dispatch is
// wired up in #528, so these tests reach the tool host directly.

#include "mcp/mcp_host.h"

#include <gtest/gtest.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/json_util.h"
#include "mcp/tool_registry.h"
#include "project/project.h"
#include "templates.h"

namespace fs = std::filesystem;
using namespace Mercury;
using Mercury::mcp::ToolRegistry;
using Mercury::mcp::ToolResult;

namespace {

fs::path TempDir(const char* name) {
  const fs::path dir =
      fs::temp_directory_path() /
      (std::string(name) + "_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  return dir;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

void Write(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

// Points XDG_CONFIG_HOME at a test directory so the editor settings (and the
// MCP token McpHost saves on first enable) never touch the user's real
// config. setenv does not exist on Windows; the tests using the guard skip
// there.
class ConfigHomeGuard {
 public:
  explicit ConfigHomeGuard(const fs::path& config_dir) : dir_(config_dir) {
    const char* previous = std::getenv("XDG_CONFIG_HOME");
    previous_ = previous ? previous : "";
    setenv("XDG_CONFIG_HOME", dir_.string().c_str(), 1);
  }
  ~ConfigHomeGuard() {
    if (previous_.empty()) {
      unsetenv("XDG_CONFIG_HOME");
    } else {
      setenv("XDG_CONFIG_HOME", previous_.c_str(), 1);
    }
  }

 private:
  fs::path dir_;
  std::string previous_;
};

// An EditorScene with its settings pointed at an isolated config dir, so a
// Save() from the MCP host cannot touch the user's editor.cfg.
struct TestScene {
  explicit TestScene(const fs::path& config_dir) : guard(config_dir) {}

  ConfigHomeGuard guard;
  EditorScene scene;
};

// Binds an OS-assigned port, remembers it and closes the socket, so a test can
// point the (disabled) MCP server at a port that is very likely free.
std::uint16_t FreePort() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    ::close(fd);
    return 0;
  }
  socklen_t length = sizeof(address);
  ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
  const std::uint16_t port = ntohs(address.sin_port);
  ::close(fd);
  return port;
}

// True when a TCP connection to 127.0.0.1:port is refused, i.e. nothing is
// listening.
bool ConnectRefused(std::uint16_t port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const bool refused = ::connect(fd, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) != 0;
  ::close(fd);
  return refused;
}

// One raw HTTP POST against the running server; returns the HTTP status, or 0
// when the request could not be completed.
int HttpPostStatus(std::uint16_t port, const std::string& authorization) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    ::close(fd);
    return 0;
  }
  const std::string request =
      "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: " +
      authorization + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  if (::write(fd, request.data(), request.size()) <
      static_cast<ssize_t>(request.size())) {
    ::close(fd);
    return 0;
  }
  std::vector<char> buffer(4096, 0);
  const ssize_t received =
      ::recv(fd, buffer.data(), buffer.size() - 1, /*flags=*/0);
  ::close(fd);
  if (received <= 0) return 0;
  // "HTTP/1.1 200 OK" -> "200"
  const std::string head(buffer.data(), static_cast<size_t>(received));
  const size_t first_space = head.find(' ');
  if (first_space == std::string::npos) return 0;
  const size_t second_space = head.find(' ', first_space + 1);
  if (second_space == std::string::npos) return 0;
  try {
    return std::stoi(head.substr(first_space + 1, second_space - first_space - 1));
  } catch (const std::exception&) {
    return 0;
  }
}

}  // namespace

// Test tools, registered through the same macro the built-ins use. They sit
// in named functions because a preprocessor macro argument may not contain
// unparenthesized commas (which a braced JSON initializer does).
namespace {

ToolResult WriteTool(EditorScene&, const nlohmann::json&) {
  return ToolResult{true, nlohmann::json{{"did", true}}, {}};
}

ToolResult EchoTool(EditorScene& scene, const nlohmann::json& args) {
  return ToolResult{
      true,
      nlohmann::json{{"echo", args.value("text", std::string())},
                     {"project", scene.GetProject() != nullptr}},
      {}};
}

}  // namespace

MERCURY_MCP_TOOL("mcp_test_write", kWrite, &WriteTool);
MERCURY_MCP_TOOL("mcp_test_echo", kRead, &EchoTool);

TEST(McpHostTest, RegistryDispatchRunsTheHandler) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  TestScene test(TempDir("mcp_dispatch"));
  auto result = ToolRegistry::Instance().Invoke(
      test.scene, "mcp_test_echo", nlohmann::json{{"text", "hi"}});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.result["echo"], "hi");
  EXPECT_EQ(result.result["project"], false);
}

TEST(McpHostTest, UnknownToolFailsWithTheContractError) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  TestScene test(TempDir("mcp_unknown"));
  auto result = ToolRegistry::Instance().Invoke(
      test.scene, "definitely_not_a_tool", nlohmann::json::object());
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.result["error"], "unknown tool definitely_not_a_tool");
}

TEST(McpHostTest, WriteToolsAreRejectedUntilEditsAreAllowed) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  TestScene test(TempDir("mcp_mode"));
  EditorSettings& settings = test.scene.Settings();
  settings.mcp_allow_edits = false;
  auto denied = ToolRegistry::Instance().Invoke(
      test.scene, "mcp_test_write", nlohmann::json::object());
  EXPECT_FALSE(denied.ok);
  EXPECT_TRUE(denied.result["error"].is_string());

  settings.mcp_allow_edits = true;
  auto allowed = ToolRegistry::Instance().Invoke(
      test.scene, "mcp_test_write", nlohmann::json::object());
  EXPECT_TRUE(allowed.ok);
  EXPECT_EQ(allowed.result["did"], true);
}

TEST(McpHostTest, BuiltinToolsAnswer) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  TestScene test(TempDir("mcp_builtin"));

  auto ping = ToolRegistry::Instance().Invoke(test.scene, "ping",
                                              nlohmann::json::object());
  EXPECT_TRUE(ping.ok);
  EXPECT_EQ(ping.result["pong"], true);
  EXPECT_TRUE(ping.result["editor_version"].is_string());

  auto list = ToolRegistry::Instance().Invoke(test.scene, "__resources/list",
                                              nlohmann::json::object());
  EXPECT_TRUE(list.ok);
  EXPECT_TRUE(list.result["resources"].is_array());
  EXPECT_TRUE(list.result["templates"].is_array());

  auto read = ToolRegistry::Instance().Invoke(
      test.scene, "__resources/read", nlohmann::json{{"uri", "x"}});
  EXPECT_FALSE(read.ok);
}

// ----------------------------------------------------------- .mcp.json ----

TEST(McpHostTest, McpJsonMergeSetsOnlyOurEntry) {
  const fs::path root = TempDir("mcp_json_merge");
  Write(root / ".mcp.json", R"({
  "other": {"keep": true},
  "mcpServers": {"someone_else": {"url": "http://example.invalid"}}
})");

  std::string error;
  ASSERT_TRUE(McpHost::WriteProjectMcpJson(
      root, "http://127.0.0.1:49152/mcp", "secret", &error))
      << error;

  nlohmann::json document;
  ASSERT_TRUE(mcp::ReadJsonFile(root / ".mcp.json", document, &error)) << error;
  EXPECT_EQ(document["other"]["keep"], true);
  EXPECT_EQ(document["mcpServers"]["someone_else"]["url"],
            "http://example.invalid");
  const nlohmann::json& ours = document["mcpServers"]["mercury"];
  EXPECT_EQ(ours["type"], "http");
  EXPECT_EQ(ours["url"], "http://127.0.0.1:49152/mcp");
  EXPECT_EQ(ours["headers"]["Authorization"], "Bearer secret");
}

TEST(McpHostTest, McpJsonRemoveKeepsOtherKeys) {
  const fs::path root = TempDir("mcp_json_remove");
  McpHost::WriteProjectMcpJson(root, "http://127.0.0.1:49152/mcp", "secret");
  Write(root / ".mcp.json", R"({
  "other": {"keep": true},
  "mcpServers": {
    "someone_else": {"url": "http://example.invalid"},
    "mercury": {"type": "http", "url": "http://127.0.0.1:1/mcp"}
  }
})");

  std::string error;
  ASSERT_TRUE(McpHost::RemoveProjectMcpJson(root, &error)) << error;
  nlohmann::json document;
  ASSERT_TRUE(mcp::ReadJsonFile(root / ".mcp.json", document, &error)) << error;
  EXPECT_EQ(document["other"]["keep"], true);
  EXPECT_EQ(document["mcpServers"]["someone_else"]["url"],
            "http://example.invalid");
  EXPECT_FALSE(document["mcpServers"].contains("mercury"));
}

TEST(McpHostTest, McpJsonRemoveDeletesAnEmptyFile) {
  const fs::path root = TempDir("mcp_json_empty");
  McpHost::WriteProjectMcpJson(root, "http://127.0.0.1:49152/mcp", "secret");

  std::string error;
  ASSERT_TRUE(McpHost::RemoveProjectMcpJson(root, &error)) << error;
  EXPECT_FALSE(fs::exists(root / ".mcp.json"));

  // Removing without a file is fine, too.
  EXPECT_TRUE(McpHost::RemoveProjectMcpJson(root, &error));
}

TEST(McpHostTest, McpJsonMergeNeverOverwritesUnparseableContent) {
  const fs::path root = TempDir("mcp_json_broken");
  Write(root / ".mcp.json", "{not json");
  std::string error;
  EXPECT_FALSE(McpHost::WriteProjectMcpJson(
      root, "http://127.0.0.1:49152/mcp", "secret", &error));
  EXPECT_EQ(Read(root / ".mcp.json"), "{not json");
}

// A running server with .mcp.json written, then write_config switched off:
// the entry must disappear without stopping the server.
TEST(McpHostTest, DisablingWriteConfigRemovesTheEntryWhileRunning) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  TestScene test(TempDir("mcp_writetoggle"));
  std::string error;
  // Inside the per-run temp dir: CreateProject refuses a pre-existing target.
  const fs::path mproj = Templates::CreateProject(
      TempDir("mcp_writetoggle_project"), "McpWriteToggle", false, &error);
  ASSERT_FALSE(mproj.empty()) << error;
  auto project = Project::Load(mproj, &error);
  ASSERT_TRUE(project != nullptr) << error;

  EditorSettings& settings = test.scene.Settings();
  settings.mcp_enabled = true;
  settings.mcp_ephemeral = true;
  settings.mcp_write_config = true;
  test.scene.SetProject(project);
  test.scene.Mcp().ApplySettings();
  ASSERT_TRUE(test.scene.Mcp().Running());

  const fs::path root = project->GetRoot();
  nlohmann::json document;
  ASSERT_TRUE(mcp::ReadJsonFile(root / ".mcp.json", document, &error)) << error;
  EXPECT_EQ(document["mcpServers"]["mercury"]["url"], test.scene.Mcp().Url());

  settings.mcp_write_config = false;
  test.scene.Mcp().ApplySettings();
  EXPECT_TRUE(test.scene.Mcp().Running()) << "the server itself stays up";
  EXPECT_FALSE(fs::exists(root / ".mcp.json"));
}

// ------------------------------------------------- Settings & life cycle --

TEST(McpHostTest, SettingsRoundTrip) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  const fs::path config = TempDir("mcp_settings");
  {
    ConfigHomeGuard guard(config);
    EditorSettings settings;
    settings.mcp_enabled = true;
    settings.mcp_port = 50001;
    settings.mcp_ephemeral = true;
    settings.mcp_allow_edits = true;
    settings.mcp_write_config = false;
    settings.mcp_token = "abcd";
    settings.Save();
  }
  ConfigHomeGuard guard(config);
  EditorSettings settings;
  settings.Load();
  EXPECT_TRUE(settings.mcp_enabled);
  EXPECT_EQ(settings.mcp_port, 50001);
  EXPECT_TRUE(settings.mcp_ephemeral);
  EXPECT_TRUE(settings.mcp_allow_edits);
  EXPECT_FALSE(settings.mcp_write_config);
  EXPECT_EQ(settings.mcp_token, "abcd");
}

TEST(McpHostTest, DisabledMeansNoServerAndNoSocket) {
#if defined(_WIN32)
  GTEST_SKIP() << "the test changes XDG_CONFIG_HOME";
#endif
  const std::uint16_t port = FreePort();
  ASSERT_NE(port, 0) << "no free port";
  TestScene test(TempDir("mcp_disabled"));
  EditorSettings& settings = test.scene.Settings();
  settings.mcp_enabled = false;
  settings.mcp_port = port;
  test.scene.Mcp().ApplySettings();
  EXPECT_FALSE(test.scene.Mcp().Running());
  EXPECT_TRUE(test.scene.Mcp().Url().empty());
  EXPECT_TRUE(ConnectRefused(port)) << "something listens on " << port;
}

#if !defined(_WIN32)
TEST(McpHostTest, EphemeralServerListensAndStops) {
  TestScene test(TempDir("mcp_ephemeral"));
  EditorSettings& settings = test.scene.Settings();
  settings.mcp_enabled = true;
  settings.mcp_ephemeral = true;
  settings.mcp_write_config = false;  // no project open; keep the test local
  test.scene.Mcp().ApplySettings();
  ASSERT_TRUE(test.scene.Mcp().Running());
  EXPECT_TRUE(test.scene.Mcp().LastError().empty());
  const std::string url = test.scene.Mcp().Url();
  EXPECT_TRUE(url.starts_with("http://127.0.0.1:"));
  EXPECT_TRUE(url.ends_with("/mcp"));
  // First enable generated a token.
  EXPECT_EQ(settings.mcp_token.size(), 64u);

  // The port from the URL is really listening, and the bearer token is
  // accepted (the MCP service answers however it likes; it must not be 401).
  const std::uint16_t port = static_cast<std::uint16_t>(
      std::stoi(url.substr(std::string("http://127.0.0.1:").size())));
  int status = HttpPostStatus(port, "Bearer " + settings.mcp_token);
  EXPECT_NE(status, 401) << url;
  status = HttpPostStatus(port, "Bearer wrong");
  EXPECT_EQ(status, 401) << url;

  // A second ApplySettings with unchanged settings must not restart.
  const std::string url_again = [&] {
    test.scene.Mcp().ApplySettings();
    return test.scene.Mcp().Url();
  }();
  EXPECT_EQ(url_again, url);

  // Disabled: the socket is gone.
  settings.mcp_enabled = false;
  test.scene.Mcp().ApplySettings();
  EXPECT_FALSE(test.scene.Mcp().Running());
  EXPECT_TRUE(ConnectRefused(port)) << "something still listens on " << port;
}
#endif  // !_WIN32
