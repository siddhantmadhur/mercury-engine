// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test-only path helpers. CMake points MERCURY_TEST_CACHE_DIR at the build
// tree; fall back to the system temp directory when it is not set.
#ifndef MERCURY_TESTS_TEST_PATHS_H
#define MERCURY_TESTS_TEST_PATHS_H

#include <cstdlib>
#include <filesystem>

namespace Mercury::test {

inline std::filesystem::path CacheDir() {
  if (const char* dir = std::getenv("MERCURY_TEST_CACHE_DIR"); dir && *dir) {
    return dir;
  }
  return std::filesystem::temp_directory_path() / "mercury_test_cache";
}

}  // namespace Mercury::test

#endif  // MERCURY_TESTS_TEST_PATHS_H
