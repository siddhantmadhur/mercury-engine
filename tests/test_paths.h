// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test-only path helpers. Bazel gives each test its own TEST_TMPDIR; fall back
// to the system temp directory when MERCURY_TEST_CACHE_DIR is not set.
#ifndef MERCURY_TESTS_TEST_PATHS_H
#define MERCURY_TESTS_TEST_PATHS_H

#include <cstdlib>
#include <filesystem>

namespace Mercury::test {

inline std::filesystem::path CacheDir() {
  if (const char* dir = std::getenv("MERCURY_TEST_CACHE_DIR"); dir && *dir) {
    return dir;
  }
  if (const char* tmp = std::getenv("TEST_TMPDIR"); tmp && *tmp) {
    return std::filesystem::path(tmp) / "mercury_test_cache";
  }
  return std::filesystem::temp_directory_path() / "mercury_test_cache";
}

}  // namespace Mercury::test

#endif  // MERCURY_TESTS_TEST_PATHS_H
