/*
 * console.h
 *
 * Log lines shown in the editor's Console panel: the editor's own messages,
 * engine logs (through PDebug's sink), the output of builds and the game, and
 * Lore version control commands.
 */
#ifndef MERCURY_EDITOR_CONSOLE_H
#define MERCURY_EDITOR_CONSOLE_H

#include <deque>
#include <mutex>
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

  void Info(std::string text) { Add(Level::kInfo, Source::kEditor, std::move(text)); }
  void Warn(std::string text) { Add(Level::kWarning, Source::kEditor, std::move(text)); }
  void Error(std::string text) { Add(Level::kError, Source::kEditor, std::move(text)); }

  void Clear();
  std::vector<Line> Snapshot();
  size_t GetVersion();  // changes whenever a line is added or cleared

 private:
  static constexpr size_t kMaxLines = 5000;
  std::mutex mutex_;
  std::deque<Line> lines_;
  size_t version_ = 0;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_CONSOLE_H
