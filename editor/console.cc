#include "console.h"

#include <algorithm>
#include <cctype>

namespace Mercury {

void Console::Add(Level level, Source source, std::string text) {
  std::lock_guard lock(mutex_);
  lines_.push_back({level, source, std::move(text)});
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

size_t Console::GetVersion() {
  std::lock_guard lock(mutex_);
  return version_;
}

}  // namespace Mercury
