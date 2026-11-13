#include "bindings.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include "debugger/debugger.h"
#include "os/filesystem.h"

#ifndef MERCURY_ENGINE_VERSION
#define MERCURY_ENGINE_VERSION "dev"
#endif

namespace Mercury::rust {

namespace {

std::string_view ToStringView(::rust::Str s) {
  return std::string_view(s.data(), s.size());
}

std::filesystem::path ToPath(::rust::Str s) {
  return std::filesystem::path(ToStringView(s));
}

}  // namespace

void LogDebug(::rust::Str message) { PDebug::log("{}", ToStringView(message)); }

void LogInfo(::rust::Str message) { PDebug::info("{}", ToStringView(message)); }

void LogWarn(::rust::Str message) { PDebug::warn("{}", ToStringView(message)); }

void LogError(::rust::Str message) { PDebug::error("{}", ToStringView(message)); }

bool OsExists(::rust::Str path) { return Mercury::os::Exists(ToPath(path)); }

bool OsIsPacked(::rust::Str path) { return Mercury::os::IsPacked(ToPath(path)); }

bool OsHasMounts() { return Mercury::os::HasMounts(); }

bool OsReadBytes(::rust::Str path, ::rust::Vec<uint8_t>& out) {
  auto bytes = Mercury::os::ReadBytes(ToPath(path));
  if (!bytes) return false;
  out.clear();
  out.reserve(bytes->size());
  std::copy(bytes->begin(), bytes->end(), std::back_inserter(out));
  return true;
}

::rust::String OsCacheDir() {
  return ::rust::String(Mercury::os::CacheDir().generic_string());
}

::rust::String OsExecutableDir() {
  return ::rust::String(Mercury::os::ExecutableDir().generic_string());
}

::rust::Str EngineVersion() { return ::rust::Str(MERCURY_ENGINE_VERSION); }

}  // namespace Mercury::rust
