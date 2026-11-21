// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * cmake_build.h
 *
 * Helpers for the two CMake trees the editor drives:
 *
 *  - the project's own build, <project>/.mercury/build/<config>, configured
 *    against the SDK the editor is running from (-DMercury_DIR=...), and
 *  - the engine checkout's build, which a development editor uses for its
 *    mercury_player (and for Build Game's Release player).
 *
 * Play and the extension loader always use the project tree. A project's
 * CMakeLists.txt and CMakePresets.json are written by project_scaffold.h.
 */
#ifndef MERCURY_EDITOR_CMAKE_BUILD_H
#define MERCURY_EDITOR_CMAKE_BUILD_H

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "os/filesystem.h"

namespace Mercury::cmake_build {

// The build directory CMake configured this checkout into. A development
// editor finds it through the engine SDK resolver; a packaged editor has no
// build tree and returns an empty path.
inline std::filesystem::path BuildDir() { return os::EngineSdkDir().build_dir; }

// True for the Debug configuration. Used to keep developer-only behavior
// (such as the Metal HUD) out of release builds.
inline bool IsDebugBuild() {
#if defined(NDEBUG)
  return false;
#else
  return true;
#endif
}

// The CMake package the editor's SDK exposes, or empty when it has none (an
// old development checkout that was not reconfigured yet).
inline std::filesystem::path SdkCmakeDir() {
  const os::EngineSdkPaths sdk = os::EngineSdkDir();
  if (sdk.cmake_dir.empty()) return {};
  std::error_code ec;
  if (!std::filesystem::exists(sdk.cmake_dir / "MercuryConfig.cmake", ec)) {
    return {};
  }
  return sdk.cmake_dir;
}

inline std::filesystem::path ProjectExecutable(
    const std::filesystem::path& build_dir, const std::string& target) {
  if (build_dir.empty()) return {};
#ifdef _WIN32
  return build_dir / "bin" / (target + ".exe");
#else
  return build_dir / "bin" / target;
#endif
}

// Configure arguments for a project tree. The editor passes the SDK it is
// running from, so the same project builds from any editor location.
inline std::vector<std::string> ConfigureArguments(
    const std::filesystem::path& source_dir,
    const std::filesystem::path& build_dir,
    const std::filesystem::path& sdk_cmake_dir,
    std::string_view build_type = "Debug") {
  return {"cmake",
          "-S",
          source_dir.string(),
          "-B",
          build_dir.string(),
          "-G",
          "Ninja",
          "-DCMAKE_BUILD_TYPE=" + std::string(build_type),
          "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
          "-DMercury_DIR=" + sdk_cmake_dir.string()};
}

// True when there is no cache yet, or CMakeLists.txt/CMakePresets.json changed
// since the last configure.
inline bool NeedsConfigure(const std::filesystem::path& source_dir,
                           const std::filesystem::path& build_dir) {
  std::error_code ec;
  const std::filesystem::path cache = build_dir / "CMakeCache.txt";
  if (!std::filesystem::exists(cache, ec)) return true;
  const auto cache_time = std::filesystem::last_write_time(cache, ec);
  if (ec) return true;
  for (const char* name : {"CMakeLists.txt", "CMakePresets.json"}) {
    const std::filesystem::path file = source_dir / name;
    if (!std::filesystem::exists(file, ec)) continue;
    const auto file_time = std::filesystem::last_write_time(file, ec);
    if (!ec && file_time > cache_time) return true;
  }
  return false;
}

// `cmake --build <build-dir> --target <target>`.
inline std::vector<std::string> BuildArguments(
    const std::filesystem::path& build_dir, const std::string& target) {
  return {"cmake", "--build", build_dir.string(), "--target", target};
}

// One project build at a time: Play, Build Game and the systems loader all
// configure/build the same .mercury/build/<config> tree, and two concurrent
// CMake processes would race in it. The editor drives all of them from its UI
// thread, so a plain flag is enough; a stale holder is released by the code
// that acquired it (including cancel/teardown).
inline std::filesystem::path& ActiveProjectBuild() {
  static std::filesystem::path active;
  return active;
}

inline bool TryAcquireProjectBuild(const std::filesystem::path& root) {
  std::filesystem::path& active = ActiveProjectBuild();
  if (!active.empty()) return false;
  active = root;
  return true;
}

inline void ReleaseProjectBuild(const std::filesystem::path& root) {
  std::filesystem::path& active = ActiveProjectBuild();
  if (active == root) active.clear();
}

inline bool IsProjectBuildBusy() { return !ActiveProjectBuild().empty(); }

}  // namespace Mercury::cmake_build

#endif  // MERCURY_EDITOR_CMAKE_BUILD_H
