// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "mcp/tools/console_json.h"

#include <algorithm>
#include <format>

namespace Mercury::mcp {

namespace {

using nlohmann::json;

const char* LevelName(Console::Level level) {
  switch (level) {
    case Console::Level::kInfo:
      return "info";
    case Console::Level::kWarning:
      return "warning";
    case Console::Level::kError:
      return "error";
  }
  return "info";
}

const char* SourceName(Console::Source source) {
  switch (source) {
    case Console::Source::kEditor:
      return "editor";
    case Console::Source::kBuild:
      return "build";
    case Console::Source::kGame:
      return "game";
    case Console::Source::kLore:
      return "lore";
  }
  return "editor";
}

}  // namespace

bool ParseConsoleLimit(const json& args, size_t default_limit, size_t max_limit,
                       size_t* out, std::string* error) {
  *out = default_limit;
  if (!args.contains("limit") || args.at("limit").is_null()) return true;
  const json& limit = args.at("limit");
  if (!limit.is_number_integer() || limit.get<int64_t>() < 1) {
    *error =
        std::format("\"limit\" must be an integer from 1 to {}", max_limit);
    return false;
  }
  *out =
      static_cast<size_t>(std::min<int64_t>(limit.get<int64_t>(), max_limit));
  return true;
}

bool ParseConsoleCursor(const json& args, Console& console,
                        std::optional<uint64_t>* out, std::string* error) {
  out->reset();
  if (!args.contains("cursor") || args.at("cursor").is_null()) return true;
  const json& value = args.at("cursor");
  if (!value.is_number_integer() || value.get<int64_t>() < 0) {
    *error =
        "\"cursor\" must be a non-negative integer: the next_cursor of a "
        "previous console-reading tool call";
    return false;
  }
  *out = value.get<uint64_t>();
  if (*out > console.LatestCursor()) {
    *error = std::format(
        "cursor {} is ahead of the console (its latest cursor is {}); it "
        "probably came from an earlier editor session. Omit \"cursor\" to "
        "read the most recent entries",
        **out, console.LatestCursor());
    return false;
  }
  return true;
}

json ConsoleEntryToJson(const Console::Entry& entry) {
  return {{"seq", entry.seq},
          {"level", LevelName(entry.line.level)},
          {"source", SourceName(entry.line.source)},
          {"text", entry.line.text}};
}

json ConsolePageToJson(const Console::Page& page) {
  json entries = json::array();
  for (const Console::Entry& entry : page.entries) {
    entries.push_back(ConsoleEntryToJson(entry));
  }
  return {{"entries", std::move(entries)},
          {"next_cursor", page.next_cursor},
          {"truncated", page.truncated},
          {"dropped", page.dropped}};
}

}  // namespace Mercury::mcp
