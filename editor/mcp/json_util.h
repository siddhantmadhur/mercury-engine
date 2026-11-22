// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * json_util.h
 *
 * JSON helpers for the MCP host. nlohmann::json throws on malformed input;
 * these wrappers turn that into return values so the host's error paths stay
 * explicit, and they provide the one thing the .mcp.json handling must not do
 * without: an atomic file write.
 */
#ifndef MERCURY_EDITOR_MCP_JSON_UTIL_H
#define MERCURY_EDITOR_MCP_JSON_UTIL_H

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Mercury::mcp {

// Parses `text` as JSON. Returns false instead of throwing on malformed input;
// `out` is left untouched in that case.
bool ParseJson(std::string_view text, nlohmann::json& out);

// Reads `path` as JSON. A missing file is not an error: `out` becomes an empty
// object. A file that exists but does not parse is an error, so callers never
// overwrite data they could not read back.
bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& out,
                  std::string* error = nullptr);

// Writes `value` to `path` as JSON atomically: the content first goes to a
// temporary file in the same folder, so a crash or a full disk never leaves a
// half-written file, and the rename then replaces any existing file.
bool WriteJsonFile(const std::filesystem::path& path,
                   const nlohmann::json& value, std::string* error = nullptr);

}  // namespace Mercury::mcp

#endif  // MERCURY_EDITOR_MCP_JSON_UTIL_H
