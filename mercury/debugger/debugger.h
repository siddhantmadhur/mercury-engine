// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURY_DEBUGGER_IMPL_H_
#define MERCURY_DEBUGGER_IMPL_H_
#include <chrono>
#include <ctime>
#include <format>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace PDebug {

enum class Level { kDebug, kInfo, kWarn, kError };

/**
 * @brief Optional callback that receives every message after it is printed
 *
 * The editor uses this to mirror engine logs into its console panel. It can be
 * called from any thread (physics callbacks log from Jolt workers), so the
 * sink must do its own locking.
 */
using Sink = std::function<void(Level level, const std::string& message)>;

inline Sink& GetSink() {
  static Sink sink;
  return sink;
}

// Guards both the sink itself and the output streams in Emit.
inline std::mutex& SinkMutex() {
  static std::mutex mutex;
  return mutex;
}

inline void SetSink(Sink sink) {
  std::lock_guard lock(SinkMutex());
  GetSink() = std::move(sink);
}

inline void Emit(Level level, std::ostream& stream, const char* tag,
                 const std::string& message) {
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  Sink sink;
  {
    std::lock_guard lock(SinkMutex());
    std::tm time_struct = *std::localtime(&now_c);

    stream << tag << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ")
           << message << std::endl;
    // Called after the lock is released: the sink may log again, and a
    // std::mutex is not recursive.
    sink = GetSink();
  }
  if (sink) sink(level, message);
}

template <class... Args>
void log(std::format_string<Args...> fmt, Args&&... args) {
  Emit(Level::kDebug, std::cerr, "[DEBUG]",
       std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
  Emit(Level::kWarn, std::cerr, "[WARN]",
       std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
  Emit(Level::kInfo, std::cout, "[INFO]",
       std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
  Emit(Level::kError, std::cerr, "[ERROR]",
       std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace PDebug

#endif
