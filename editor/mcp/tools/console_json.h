// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * console_json.h
 *
 * The part of reading the editor's Console that every console-reading MCP
 * tool needs: validating a cursor and a limit, and turning Console entries and
 * pages into the JSON shape tools return. The tools differ only in how they
 * choose the minimum level and which sources they keep; see read_console in
 * scene_read.cc (all sources) and play_logs in play.cc (build and game only).
 *
 * INTERNAL header (it includes nlohmann/json): excluded from the SDK install,
 * like mcp/tool_registry.h and scene_json.h.
 */
#ifndef MERCURY_EDITOR_MCP_TOOLS_CONSOLE_JSON_H
#define MERCURY_EDITOR_MCP_TOOLS_CONSOLE_JSON_H

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "console.h"

namespace Mercury::mcp {

/**
 * @brief Validates the "limit" argument shared by the console-reading tools
 *
 * An integer from 1 to `max_limit`, clamped to `max_limit`; absent or null
 * means `default_limit`.
 *
 * @return false with `error` set when the value is present and invalid.
 */
bool ParseConsoleLimit(const nlohmann::json& args, size_t default_limit,
                       size_t max_limit, size_t* out, std::string* error);

/**
 * @brief Validates the "cursor" argument shared by the console-reading tools
 *
 * A non-negative integer no greater than the console's latest cursor. Absent
 * or null means the caller wants a tail of the newest lines.
 *
 * @return false with `error` set when the value is present and invalid.
 */
bool ParseConsoleCursor(const nlohmann::json& args, Console& console,
                        std::optional<uint64_t>* out, std::string* error);

/** One entry as {"seq", "level", "source", "text"}. */
nlohmann::json ConsoleEntryToJson(const Console::Entry& entry);

/** A page as {"entries", "next_cursor", "truncated", "dropped"}. */
nlohmann::json ConsolePageToJson(const Console::Page& page);

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_TOOLS_CONSOLE_JSON_H
