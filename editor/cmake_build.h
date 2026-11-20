// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Small helpers for driving the CMake build the editor was compiled from.
// Build/Play use the same build directory, target names and configuration as
// the checkout, so no environment variables or wrapper scripts are needed.
#ifndef MERCURY_EDITOR_CMAKE_BUILD_H
#define MERCURY_EDITOR_CMAKE_BUILD_H

#include <filesystem>
#include <string>
#include <vector>

#include "os/filesystem.h"

namespace Mercury::cmake_build {

// The build directory CMake configured this checkout into. A development
// editor finds it through the engine SDK resolver; a packaged editor has no
// build tree and returns an empty path.
inline std::filesystem::path BuildDir() { return os::EngineSdkDir().build_dir; }

// The profile CMake used for this build. Ninja is single-config, but
// `--config` is harmless there and required for multi-config generators.
inline std::string BuildConfig() {
#ifdef MERCURY_BUILD_CONFIG
  return MERCURY_BUILD_CONFIG;
#else
  return {};
#endif
}

// True for the Debug configuration. Used to keep developer-only behavior
// (such as the Metal HUD) out of release builds.
inline bool IsDebugBuild() { return BuildConfig() == "Debug"; }

inline std::vector<std::string> BuildArguments(const std::string& target) {
  std::vector<std::string> args = {"cmake", "--build", BuildDir().string()};
  const std::string config = BuildConfig();
  if (!config.empty()) {
    args.push_back("--config");
    args.push_back(config);
  }
  args.push_back("--target");
  args.push_back(target);
  return args;
}

// CMake writes every executable to <build>/bin (see the output directories in
// the top-level CMakeLists.txt).
inline std::filesystem::path Executable(const std::string& target) {
  const std::filesystem::path dir = BuildDir();
  if (dir.empty()) return {};
#ifdef _WIN32
  return dir / "bin" / (target + ".exe");
#else
  return dir / "bin" / target;
#endif
}

}  // namespace Mercury::cmake_build

#endif  // MERCURY_EDITOR_CMAKE_BUILD_H
