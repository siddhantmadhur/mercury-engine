// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * assets_build.h
 *
 * The parts of the assets/validation/build tool group (assets_build.cc) that
 * its tests reach directly.
 *
 * INTERNAL header: it is excluded from the SDK install, like
 * mcp/tool_registry.h.
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_ASSETS_BUILD_H
#define MERCURY_EDITOR_MCP_TOOLS_ASSETS_BUILD_H

#include <string>
#include <string_view>
#include <vector>

namespace Mercury::mcp {

/**
 * @brief One compiler, AngelScript or CMake message found in build output
 */
struct BuildDiagnostic {
  // As the tool printed it, except that a path inside the project is made
  // relative to the project root ("scripts/player.as"). "" when the message
  // names no file.
  std::string file;
  int line = 0;  // 1-based; 0 when the message has none
  int col = 0;   // 1-based; 0 when the message has none
  // "error", "warning" or "note" (context a tool attaches to another
  // message: a gcc/clang "note:", an AngelScript information message).
  std::string severity;
  std::string message;
};

/**
 * @brief Finds the diagnostics in the lines a build printed
 *
 * Understands four formats (the same four build_scripts documents):
 *
 *   AngelScript (mercury_player, through PDebug)
 *       [ERROR] 2026-11-21 10:15:42 scripts/player.as:12:5: Expected ';'
 *       The tag is ERROR, WARN or INFO (INFO becomes a "note"); the file is
 *       the script's project-relative path; row and column are 1-based.
 *   GCC / Clang
 *       src/a.cc:12:5: error: expected ';' after expression
 *       The column is optional; severity is error, fatal error, warning or
 *       note. "clang: error: linker command failed" (a tool name instead of
 *       a file) is kept as a message with no file.
 *   MSVC
 *       src\a.cc(12,5): error C2143: syntax error
 *       The column and the error code are optional.
 *   CMake
 *       CMake Error at CMakeLists.txt:12 (add_executable):
 *         Cannot find source file: x.cc
 *       The message is the indented text under the header (continuation
 *       lines are joined with a space); "CMake Warning" and "CMake Warning
 *       (dev)" are warnings; "CMake Error: text" has no file.
 *
 * Lines that match none of these are not diagnostics. `project_root` (may be
 * empty) is stripped from the front of absolute file names.
 */
std::vector<BuildDiagnostic> ParseBuildDiagnostics(
    const std::vector<std::string>& lines, std::string_view project_root);

/**
 * @brief Whether `path` matches `pattern` as the list_project_assets
 *        `path_glob` argument means it
 *
 * "*" matches any run of characters except "/", "**" any run including "/"
 * (and "**" followed by "/" also matches no folder at all, so "a/**" + "/x"
 * matches "a/x"), "?" one character except "/". Case-sensitive.
 */
bool GlobMatch(std::string_view pattern, std::string_view path);

/**
 * @brief Advances the running build_scripts call, if there is one
 *
 * The tool arms a wxTimer that calls this every 100 ms in the editor. A
 * process without a wxApp (the tool tests) has no timer; its pump calls this
 * instead. Does nothing when no build_scripts call is pending. On the UI
 * thread.
 */
void TickScriptBuild();

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOLS_ASSETS_BUILD_H
