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
