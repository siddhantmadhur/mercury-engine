// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test-only path helpers. The test cache defaults to the build tree (one
// folder per checkout and build preset, so concurrent check builds never
// share it); an explicit MERCURY_TEST_CACHE_DIR environment variable wins,
// and without CMake's definition the helper falls back to the system temp
// directory.
#ifndef MERCURY_TESTS_TEST_PATHS_H
#define MERCURY_TESTS_TEST_PATHS_H

#include <cstdlib>
#include <filesystem>

namespace Mercury::test {

inline std::filesystem::path CacheDir() {
  if (const char* dir = std::getenv("MERCURY_TEST_CACHE_DIR"); dir && *dir) {
    return dir;
  }
#ifdef MERCURY_TEST_CACHE_DIR
  return MERCURY_TEST_CACHE_DIR;
#else
  return std::filesystem::temp_directory_path() / "mercury_test_cache";
#endif
}

// The benchmark project (models and maps too big for this repo) lives beside
// it by default; CMake's MERCURY_BENCHMARK_DIR cache variable, or the
// environment variable of the same name, point elsewhere. Tests that need
// its models skip when they are not there.
inline std::filesystem::path BenchmarkDir() {
  if (const char* dir = std::getenv("MERCURY_BENCHMARK_DIR"); dir && *dir) {
    return dir;
  }
#ifdef MERCURY_BENCHMARK_DIR
  return MERCURY_BENCHMARK_DIR;
#else
  return std::filesystem::path(MERCURY_SOURCE_DIR) / ".." / "benchmark";
#endif
}

}  // namespace Mercury::test

#endif  // MERCURY_TESTS_TEST_PATHS_H
