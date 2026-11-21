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

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "editor_scene.h"
#include "editor_settings.h"
#include "mcp/json_util.h"
#include "mcp/tool_registry.h"
#include "project/project.h"
#include "templates.h"
#include "version.h"

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

// One raw HTTP POST with a JSON-RPC body; returns the whole response text, or
// the empty string when the request could not be completed. Sends the headers
// the 2026-07-28 protocol requires (SEP-2243): Mcp-Protocol-Version,
// Mcp-Method and, for tools/call, Mcp-Name with the tool name.
//
// `host` overrides the Host header (default 127.0.0.1:<port>) and `origin`,
// when non-empty, adds an Origin header: the security probes use them.
std::string HttpPost(std::uint16_t port, const std::string& authorization,
                     const std::string& body, const std::string& host = "",
                     const std::string& origin = "") {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "";
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    ::close(fd);
    return "";
  }

  nlohmann::json request_json;
  std::string method;
  std::string name;
  // An empty body is a bare probe (status-line only); a JSON-RPC body must
  // parse, because its method also drives the SEP-2243 headers.
  if (!body.empty()) {
    try {
      request_json = nlohmann::json::parse(body);
      method = request_json["method"].get<std::string>();
      if (method == "tools/call") {
        name = request_json["params"]["name"].get<std::string>();
      }
    } catch (const std::exception&) {
      ::close(fd);
      return "";
    }
  }

  const std::string host_header =
      host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
  std::string request = "POST /mcp HTTP/1.1\r\nHost: " + host_header +
                        "\r\n"
                        "Accept: application/json, text/event-stream\r\n"
                        "Content-Type: application/json\r\n";
  if (!origin.empty()) {
    request += "Origin: " + origin + "\r\n";
  }
  if (!method.empty()) {
    request += "Mcp-Protocol-Version: 2026-07-28\r\n";
    request += "Mcp-Method: " + method + "\r\n";
    if (!name.empty()) {
      request += "Mcp-Name: " + name + "\r\n";
    }
  }
  request += "Authorization: " + authorization + "\r\n";
  request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  request += "Connection: close\r\n\r\n";
  request += body;
  if (::write(fd, request.data(), request.size()) <
      static_cast<ssize_t>(request.size())) {
    ::close(fd);
    return "";
  }

  // Read to EOF (Connection: close), so a multi-packet response arrives whole.
  std::string response;
  std::vector<char> buffer(4096, 0);
  for (;;) {
    const ssize_t received =
        ::recv(fd, buffer.data(), buffer.size(), /*flags=*/0);
    if (received <= 0) break;
    response.append(buffer.data(), static_cast<size_t>(received));
  }
  ::close(fd);
  return response;
}

// The HTTP status of a raw response, or 0 when it has no parsable status line.
int StatusOf(const std::string& response) {
  if (response.empty()) return 0;
  // "HTTP/1.1 200 OK" -> "200"
  const size_t first_space = response.find(' ');
  if (first_space == std::string::npos) return 0;
  const size_t second_space = response.find(' ', first_space + 1);
  if (second_space == std::string::npos) return 0;
  try {
    return std::stoi(
        response.substr(first_space + 1, second_space - first_space - 1));
  } catch (const std::exception&) {
    return 0;
  }
}

// One bare POST (no JSON-RPC body) against the running server; returns the
// HTTP status, or 0 when the request could not be completed. `host` and
// `origin` override the Host and Origin headers when non-empty.
int HttpPostStatus(std::uint16_t port, const std::string& authorization,
                   const std::string& host = "",
                   const std::string& origin = "") {
  return StatusOf(HttpPost(port, authorization, "", host, origin));
}

// The JSON-RPC payload of an HTTP response, whether the server answered with
// a plain JSON body or an SSE stream (`data:` lines).
nlohmann::json ResponseJson(const std::string& response) {
  const size_t body_start = response.find("\r\n\r\n");
  const std::string body = body_start == std::string::npos
                               ? response
                               : response.substr(body_start + 4);
  if (body.starts_with('{')) return nlohmann::json::parse(body);
  // SSE: each event carries its payload on a `data:` line.
  for (size_t line = body.find("data:"); line != std::string::npos;
       line = body.find("data:", line + 1)) {
    const size_t end = body.find('\n', line);
    try {
      return nlohmann::json::parse(body.substr(line + 5, end - line - 5));
    } catch (const std::exception&) {
      // Not the first event payload; keep looking.
    }
  }
  return nlohmann::json();
}

// A compliant MCP 2026-07-28 request: the SEP-1319 per-request `_meta` every
// non-initialize request must carry.
nlohmann::json StatelessMeta() {
  return nlohmann::json{
      {"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
      {"io.modelcontextprotocol/clientCapabilities",
       nlohmann::json::object()}};
}

nlohmann::json JsonRpc(int id, const std::string& method,
                       const nlohmann::json& params) {
  return nlohmann::json{
      {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

// Installs the inline test dispatcher and restores the default (empty) one
// when the test ends, however it ends.
class InlineDispatcherGuard {
 public:
  InlineDispatcherGuard() {
    McpHost::SetDispatcherForTest(
        [](std::function<void()> work) { work(); });
  }
  ~InlineDispatcherGuard() { McpHost::SetDispatcherForTest({}); }
};

// Starts the scene's MCP server headlessly (ephemeral port, no .mcp.json) and
// returns the port it listens on.
std::uint16_t StartEphemeralServer(TestScene& test, bool allow_edits) {
  EditorSettings& settings = test.scene.Settings();
  settings.mcp_enabled = true;
  settings.mcp_ephemeral = true;
  settings.mcp_allow_edits = allow_edits;
  settings.mcp_write_config = false;  // no project open; keep the test local
  test.scene.Mcp().ApplySettings();
  if (!test.scene.Mcp().Running()) return 0;
  const std::string& url = test.scene.Mcp().Url();
  const size_t colon = url.rfind(':');
  if (colon == std::string::npos) return 0;
  try {
    return static_cast<std::uint16_t>(
        std::stoi(url.substr(colon + 1, url.find("/mcp") - colon - 1)));
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

// ------------------------------------------------------- End-to-end flows --
//
// The full stack over real HTTP: the MCP transport calls the tool host, the
// inline test dispatcher runs the tool work in place (instead of queueing to
// a UI thread), and the registry answers. The header dance follows the
// 2026-07-28 protocol (stateless requests with the SEP-1319 `_meta`), like a
// compliant client.

#if !defined(_WIN32)
TEST(McpHostTest, E2ePingAndServerInfoAnswerOverHttp) {
  TestScene test(TempDir("mcp_e2e_ping"));
  // Inline dispatcher: the queued work runs on the caller's thread.
  InlineDispatcherGuard dispatcher;
  const std::uint16_t port = StartEphemeralServer(test, /*allow_edits=*/true);
  ASSERT_NE(port, 0) << "the server did not start";
  const std::string token = "Bearer " + test.scene.Settings().mcp_token;

  // initialize
  const std::string init = HttpPost(
      port, token,
      JsonRpc(1, "initialize",
              nlohmann::json{{"protocolVersion", "2026-07-28"},
                             {"capabilities", nlohmann::json::object()},
                             {"clientInfo", nlohmann::json{
                                 {"name", "mcp_host_test"}, {"version", "0"}}}})
          .dump());
  ASSERT_FALSE(init.empty()) << "initialize did not answer";
  nlohmann::json payload = ResponseJson(init);
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  EXPECT_EQ(payload["result"]["serverInfo"]["version"],
            MERCURY_VERSION_STRING);

  // tools/list includes ping
  const std::string list = HttpPost(
      port, token,
      JsonRpc(2, "tools/list",
              nlohmann::json{{"_meta", StatelessMeta()}})
          .dump());
  payload = ResponseJson(list);
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  bool has_ping = false;
  bool has_server_info = false;
  for (const auto& tool : payload["result"]["tools"]) {
    has_ping = has_ping || tool["name"] == "ping";
    has_server_info = has_server_info || tool["name"] == "get_server_info";
  }
  EXPECT_TRUE(has_ping) << payload.dump();
  EXPECT_TRUE(has_server_info) << payload.dump();

  // tools/call ping: the built-in answers through the registry.
  const std::string ping = HttpPost(
      port, token,
      JsonRpc(3, "tools/call",
              nlohmann::json{{"name", "ping"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump());
  payload = ResponseJson(ping);
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  EXPECT_EQ(payload["result"]["isError"], false);
  ASSERT_TRUE(payload["result"]["structuredContent"].contains("pong"))
      << payload.dump();
  EXPECT_EQ(payload["result"]["structuredContent"]["pong"], true);
  EXPECT_EQ(payload["result"]["structuredContent"]["editor_version"],
            MERCURY_VERSION_STRING);

  // tools/call get_server_info: answered by the Rust transport with the
  // editor's version.
  const std::string info = HttpPost(
      port, token,
      JsonRpc(4, "tools/call",
              nlohmann::json{{"name", "get_server_info"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump());
  payload = ResponseJson(info);
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  EXPECT_EQ(payload["result"]["structuredContent"]["version"],
            MERCURY_VERSION_STRING);
  EXPECT_EQ(payload["result"]["structuredContent"]["read_only"], false);

  test.scene.Mcp().Stop();
  EXPECT_TRUE(ConnectRefused(port)) << "something still listens on " << port;
}

// The "Write tool rejected in read-only mode" case is NOT covered over HTTP
// here: the tool catalogue lives in Rust (mercury/mcp/src/tools), the real
// catalogue has no Write-mode tool until Wave 2 adds one, and the Rust-only
// Write spec the transport's unit tests use is compiled out of the archive
// this binary links. The gate itself is tested in mercury_mcp's cargo tests
// (the read-only test with a test-only Write spec). The HTTP case moves to T3
// (#535): its apply_scene_operations is the first real Write tool. What this
// test covers is the read-only server's
// observable behaviour: it answers Read tools, reports read_only, and a tool
// that only exists in the C++ registry is not reachable (the Rust catalogue is
// the source of truth for what a client may call).
TEST(McpHostTest, E2eReadOnlyServerAnswersReadToolsAndReportsReadOnly) {
  TestScene test(TempDir("mcp_e2e_readonly"));
  InlineDispatcherGuard dispatcher;
  const std::uint16_t port = StartEphemeralServer(test, /*allow_edits=*/false);
  ASSERT_NE(port, 0) << "the server did not start";
  const std::string token = "Bearer " + test.scene.Settings().mcp_token;

  // A Read-mode tool works on a read-only server.
  nlohmann::json payload = ResponseJson(HttpPost(
      port, token,
      JsonRpc(1, "tools/call",
              nlohmann::json{{"name", "ping"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump()));
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  EXPECT_EQ(payload["result"]["isError"], false);
  EXPECT_EQ(payload["result"]["structuredContent"]["pong"], true);

  // get_server_info reports the read-only mode and the editor's version.
  payload = ResponseJson(HttpPost(
      port, token,
      JsonRpc(2, "tools/call",
              nlohmann::json{{"name", "get_server_info"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump()));
  ASSERT_TRUE(payload.contains("result")) << payload.dump();
  EXPECT_EQ(payload["result"]["structuredContent"]["read_only"], true);
  EXPECT_EQ(payload["result"]["structuredContent"]["version"],
            MERCURY_VERSION_STRING);

  // A tool registered only in the C++ registry is not in the Rust catalogue,
  // so a client cannot call it: an MCP error (-32602), not a tool result.
  payload = ResponseJson(HttpPost(
      port, token,
      JsonRpc(3, "tools/call",
              nlohmann::json{{"name", "mcp_test_write"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump()));
  ASSERT_TRUE(payload.contains("error")) << payload.dump();
  EXPECT_EQ(payload["error"]["code"], -32602);
  EXPECT_FALSE(payload.contains("result"));

  test.scene.Mcp().Stop();
  EXPECT_TRUE(ConnectRefused(port)) << "something still listens on " << port;
}

TEST(McpHostTest, E2eSecurityChecksRejectBadTokenOriginAndHost) {
  TestScene test(TempDir("mcp_e2e_security"));
  InlineDispatcherGuard dispatcher;
  const std::uint16_t port = StartEphemeralServer(test, /*allow_edits=*/false);
  ASSERT_NE(port, 0) << "the server did not start";
  const std::string good = "Bearer " + test.scene.Settings().mcp_token;
  const std::string host = "127.0.0.1:" + std::to_string(port);

  // Wrong or missing token: 401 (Host and Origin are fine).
  EXPECT_EQ(HttpPostStatus(port, "Bearer not-the-token"), 401);
  EXPECT_EQ(HttpPostStatus(port, ""), 401);
  EXPECT_NE(HttpPostStatus(port, good), 401);

  // A foreign Origin is refused with 403 even with the right token, so a web
  // page cannot drive the editor through the user's browser.
  EXPECT_EQ(HttpPostStatus(port, good, host, "https://evil.example"), 403);
  EXPECT_EQ(HttpPostStatus(port, good, host, "http://127.0.0.1.evil.com"),
            403);
  // A loopback Origin, on any port, is allowed through.
  EXPECT_NE(HttpPostStatus(port, good, host, "http://127.0.0.1:3000"), 403);
  EXPECT_NE(HttpPostStatus(port, good, host, "http://localhost"), 403);

  // A foreign Host is refused with 403 (DNS rebinding), again with the right
  // token; `localhost:<port>` is accepted.
  EXPECT_EQ(HttpPostStatus(port, good, "evil.example:" + std::to_string(port)),
            403);
  EXPECT_EQ(HttpPostStatus(port, good, "evil.example"), 403);
  EXPECT_NE(HttpPostStatus(port, good, "localhost:" + std::to_string(port)),
            403);

  // Host and Origin are checked before the token: a foreign Origin with a
  // wrong token is still 403, not 401.
  EXPECT_EQ(HttpPostStatus(port, "Bearer not-the-token", host,
                           "https://evil.example"),
            403);

  test.scene.Mcp().Stop();
  EXPECT_TRUE(ConnectRefused(port)) << "something still listens on " << port;
}
#endif  // !_WIN32

// ------------------------------------------------- Asynchronous tool calls --
//
// The full stack over real HTTP with an asynchronous handler. The Rust
// catalogue is the source of truth for tool names, so the test re-registers
// `ping` (restored afterwards) as an async tool, and plays the UI thread
// itself: the dispatcher queues the tool work, the test thread pumps it, and a
// client thread blocks in the HTTP call until the reply completes.

#if !defined(_WIN32)
namespace {

// The queue-onto-UI-thread step as a manual pump.
class UiQueue {
 public:
  UiQueue() {
    McpHost::SetDispatcherForTest([this](std::function<void()> work) {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(std::move(work));
    });
  }
  ~UiQueue() { McpHost::SetDispatcherForTest({}); }

  // Runs what has been queued; false when nothing was.
  bool Pump() {
    std::vector<std::function<void()>> work;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      work.swap(queue_);
    }
    for (auto& item : work) item();
    return !work.empty();
  }

 private:
  std::mutex mutex_;
  std::vector<std::function<void()>> queue_;
};

// Replaces the built-in `ping` with an async handler that parks its `done`
// callback for the test, and puts the original back when the test ends.
class AsyncPingGuard {
 public:
  AsyncPingGuard() {
    const mcp::ToolEntry* original = ToolRegistry::Instance().Find("ping");
    original_ = original->handler;
    ToolRegistry::Instance().RegisterAsync(
        "ping", mcp::ToolMode::kRead,
        [this](EditorScene&, const nlohmann::json&, mcp::ToolDone done) {
          std::lock_guard<std::mutex> lock(mutex_);
          parked_ = std::move(done);
          started_ = true;
        });
  }
  ~AsyncPingGuard() {
    ToolRegistry::Instance().Register("ping", mcp::ToolMode::kRead, original_);
  }

  bool Started() {
    std::lock_guard<std::mutex> lock(mutex_);
    return started_;
  }
  mcp::ToolDone TakeDone() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(parked_);
  }

 private:
  mcp::ToolHandler original_;
  std::mutex mutex_;
  mcp::ToolDone parked_;
  bool started_ = false;
};

// Pumps the UI queue until the async handler has started (the client's call
// reached it), or ten seconds pass.
bool PumpUntilStarted(UiQueue& ui, AsyncPingGuard& ping) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!ping.Started()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    ui.Pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

std::string CallPingOverHttp(std::uint16_t port, const std::string& token) {
  return HttpPost(
      port, token,
      JsonRpc(7, "tools/call",
              nlohmann::json{{"name", "ping"},
                             {"arguments", nlohmann::json::object()},
                             {"_meta", StatelessMeta()}})
          .dump());
}

}  // namespace

TEST(McpHostTest, E2eAsyncToolCompletesLaterOnTheUiThread) {
  auto test = std::make_unique<TestScene>(TempDir("mcp_e2e_async"));
  UiQueue ui;
  AsyncPingGuard ping;
  const std::uint16_t port =
      StartEphemeralServer(*test, /*allow_edits=*/true);
  ASSERT_NE(port, 0) << "the server did not start";
  const std::string token = "Bearer " + test->scene.Settings().mcp_token;

  std::string response;
  std::thread client([&] { response = CallPingOverHttp(port, token); });
  ASSERT_TRUE(PumpUntilStarted(ui, ping)) << "the handler never ran";

  // The handler returned without answering: the call is still open. Complete
  // it a moment later, from "the UI thread" (this one).
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  mcp::ToolDone done = ping.TakeDone();
  ASSERT_TRUE(static_cast<bool>(done));
  done(ToolResult{true, nlohmann::json{{"pong", "later"}}, {}});
  // A second done() is ignored: the first answer stands, nothing crashes.
  mcp::CallTracker::SetAssertOnDoubleDone(false);
  done(ToolResult{false, nlohmann::json{{"error", "second"}}, {}});
  mcp::CallTracker::SetAssertOnDoubleDone(true);
  client.join();

  const nlohmann::json payload = ResponseJson(response);
  ASSERT_TRUE(payload.contains("result")) << response;
  EXPECT_EQ(payload["result"]["isError"], false) << payload.dump();
  EXPECT_EQ(payload["result"]["structuredContent"]["pong"], "later")
      << payload.dump();
  test->scene.Mcp().Stop();
}

TEST(McpHostTest, E2eAsyncToolThatNeverCompletesIsAnsweredWhenTheHostDies) {
  auto test = std::make_unique<TestScene>(TempDir("mcp_e2e_async_dies"));
  UiQueue ui;
  AsyncPingGuard ping;
  const std::uint16_t port =
      StartEphemeralServer(*test, /*allow_edits=*/true);
  ASSERT_NE(port, 0) << "the server did not start";
  const std::string token = "Bearer " + test->scene.Settings().mcp_token;

  std::string response;
  std::thread client([&] { response = CallPingOverHttp(port, token); });
  ASSERT_TRUE(PumpUntilStarted(ui, ping)) << "the handler never ran";
  mcp::ToolDone done = ping.TakeDone();

  // The editor goes away with the call open: the client must not hang.
  const auto started = std::chrono::steady_clock::now();
  test.reset();
  client.join();
  EXPECT_LT(std::chrono::steady_clock::now() - started,
            std::chrono::seconds(10));
  // The host answers "editor shutting down" itself; when the transport had
  // already closed the connection the client sees no response at all, which
  // is just as final.
  RecordProperty("client_saw_response", !response.empty());
  if (!response.empty()) {
    EXPECT_NE(response.find("editor shutting down"), std::string::npos)
        << response;
  }

  // A done() after the host's death is a harmless no-op: no crash, and no
  // assertion, because the host already answered the call.
  done(ToolResult{true, nlohmann::json{{"pong", "too late"}}, {}});
}
#endif  // !_WIN32
