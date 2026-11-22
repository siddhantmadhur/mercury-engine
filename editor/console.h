// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * console.h
 *
 * Log lines shown in the editor's Console panel: the editor's own messages,
 * engine logs (through PDebug's sink), the output of builds and the game, and
 * Lore version control commands.
 */
#ifndef MERCURY_EDITOR_CONSOLE_H
#define MERCURY_EDITOR_CONSOLE_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Mercury {

class Console {
 public:
  enum class Level { kInfo, kWarning, kError };
  enum class Source { kEditor, kBuild, kGame, kLore };

  struct Line {
    Level level;
    Source source;
    std::string text;
  };

  // Thread-safe; engine logs can arrive from physics worker threads.
  void Add(Level level, Source source, std::string text);
  // Guesses the level of a line of process output from its wording.
  void AddProcessOutput(Source source, std::string text);

  void Info(std::string text) {
    Add(Level::kInfo, Source::kEditor, std::move(text));
  }
  void Warn(std::string text) {
    Add(Level::kWarning, Source::kEditor, std::move(text));
  }
  void Error(std::string text) {
    Add(Level::kError, Source::kEditor, std::move(text));
  }

  // The script file a line points at: "scripts/ball.as:42:5" (an AngelScript
  // exception). Returns the project-relative path, e.g. "scripts/ball.as".
  struct ScriptLocation {
    std::string path;
    int line = 0;
    int column = 0;
  };
  static std::optional<std::string> FindScriptFile(const std::string& text);
  static std::optional<ScriptLocation> FindScriptLocation(
      const std::string& text);

  void Clear();
  std::vector<Line> Snapshot();
  size_t GetVersion();  // changes whenever a line is added or cleared

  // Sequence numbers, for readers that poll (the MCP read_console tool). Every
  // line added gets the next number, starting at 0, and a number is never
  // reused: not after the oldest lines are dropped at kMaxLines, not after
  // Clear(). A "cursor" is the number of the first line a reader has not seen.
  struct Entry {
    uint64_t seq = 0;
    Line line;
  };
  struct Page {
    std::vector<Entry> entries;
    // Where to continue: the number the next line added will get, or the one
    // after the last entry returned when `truncated`.
    uint64_t next_cursor = 0;
    // The caller's cursor was older than the oldest line still held: lines
    // between the cursor and `oldest` were dropped (or cleared) unseen.
    bool dropped = false;
    // More matching lines exist than were returned: after the returned ones
    // when a cursor was given, before them for a tail.
    bool truncated = false;
  };
  // The number the next added line will get; the cursor that reads only lines
  // added from now on.
  uint64_t LatestCursor();
  // The lines with seq >= `cursor` at or above `min_level`, oldest first, at
  // most `limit` of them. Without a cursor, the newest `limit` matching lines
  // (a tail). See Page for the other fields.
  Page ReadFrom(std::optional<uint64_t> cursor, Level min_level, size_t limit);
  // The lines currently held at each level, and the cursor at the time.
  struct Counts {
    size_t errors = 0;
    size_t warnings = 0;
    uint64_t latest_cursor = 0;
  };
  Counts CountLevels();

 private:
  static constexpr size_t kMaxLines = 5000;
  std::mutex mutex_;
  std::deque<Line> lines_;
  size_t version_ = 0;
  // lines_[i] has sequence number next_seq_ - lines_.size() + i.
  uint64_t next_seq_ = 0;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_CONSOLE_H
