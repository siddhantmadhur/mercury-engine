// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "console.h"

#include <algorithm>
#include <cctype>

namespace Mercury {

void Console::Add(Level level, Source source, std::string text) {
  std::lock_guard lock(mutex_);
  lines_.push_back({level, source, std::move(text)});
  next_seq_++;
  while (lines_.size() > kMaxLines) lines_.pop_front();
  version_++;
}

void Console::AddProcessOutput(Source source, std::string text) {
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  Level level = Level::kInfo;
  if (lower.find("error") != std::string::npos ||
      lower.find("failed") != std::string::npos) {
    level = Level::kError;
  } else if (lower.find("warn") != std::string::npos) {
    level = Level::kWarning;
  }
  Add(level, source, std::move(text));
}

std::optional<std::string> Console::FindScriptFile(const std::string& text) {
  auto location = FindScriptLocation(text);
  if (!location) return std::nullopt;
  return location->path;
}

std::optional<Console::ScriptLocation> Console::FindScriptLocation(
    const std::string& text) {
  size_t from = 0;
  while (true) {
    const size_t at = text.find("scripts/", from);
    if (at == std::string::npos) return std::nullopt;
    from = at + 1;
    const size_t as = text.find(".as", at);
    if (as == std::string::npos) return std::nullopt;
    const size_t end = as + 3;
    if (end >= text.size() || (text[end] != ':' && text[end] != '(')) continue;
    if (end + 1 >= text.size() ||
        !std::isdigit(static_cast<unsigned char>(text[end + 1])))
      continue;
    ScriptLocation location;
    location.path = text.substr(at, end - at);
    size_t cursor = end + 1;
    while (cursor < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[cursor]))) {
      location.line = location.line * 10 + (text[cursor] - '0');
      ++cursor;
    }
    if (cursor < text.size() && text[cursor] == ':') {
      ++cursor;
      while (cursor < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[cursor]))) {
        location.column = location.column * 10 + (text[cursor] - '0');
        ++cursor;
      }
    }
    return location;
  }
}

void Console::Clear() {
  std::lock_guard lock(mutex_);
  lines_.clear();
  version_++;
}

std::vector<Console::Line> Console::Snapshot() {
  std::lock_guard lock(mutex_);
  return {lines_.begin(), lines_.end()};
}

uint64_t Console::LatestCursor() {
  std::lock_guard lock(mutex_);
  return next_seq_;
}

Console::Page Console::ReadFrom(std::optional<uint64_t> cursor, Level min_level,
                                size_t limit) {
  std::lock_guard lock(mutex_);
  Page page;
  const uint64_t oldest = next_seq_ - lines_.size();
  const auto seq_of = [&](size_t index) { return oldest + index; };
  const auto wanted = [&](const Line& line) {
    return static_cast<int>(line.level) >= static_cast<int>(min_level);
  };
  page.next_cursor = next_seq_;

  if (!cursor) {
    // The tail: walk back from the newest line.
    size_t from = lines_.size();
    size_t found = 0;
    while (from > 0 && found < limit) {
      if (wanted(lines_[--from])) found++;
    }
    for (size_t i = from; i < lines_.size(); i++) {
      if (wanted(lines_[i])) page.entries.push_back({seq_of(i), lines_[i]});
    }
    for (size_t i = from; i > 0 && !page.truncated; i--) {
      page.truncated = wanted(lines_[i - 1]);
    }
    return page;
  }

  page.dropped = *cursor < oldest;
  const size_t start =
      *cursor > oldest ? static_cast<size_t>(std::min<uint64_t>(
                             *cursor - oldest, lines_.size()))
                       : 0;
  for (size_t i = start; i < lines_.size(); i++) {
    if (!wanted(lines_[i])) continue;
    if (page.entries.size() >= limit) {
      page.truncated = true;
      page.next_cursor = seq_of(i);
      break;
    }
    page.entries.push_back({seq_of(i), lines_[i]});
  }
  return page;
}

Console::Counts Console::CountLevels() {
  std::lock_guard lock(mutex_);
  Counts counts;
  for (const Line& line : lines_) {
    if (line.level == Level::kError) counts.errors++;
    if (line.level == Level::kWarning) counts.warnings++;
  }
  counts.latest_cursor = next_seq_;
  return counts;
}

size_t Console::GetVersion() {
  std::lock_guard lock(mutex_);
  return version_;
}

}  // namespace Mercury
