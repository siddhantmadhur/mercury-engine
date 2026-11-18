// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// os::UserPath: command-line paths stay exactly as the user typed them, so the
// program's working directory resolves relative paths and errors name the
// typed path.
#include <gtest/gtest.h>

#include <filesystem>

#include "os/filesystem.h"

namespace fs = std::filesystem;

namespace {

TEST(UserPathTest, RelativePathsAreUnchanged) {
  EXPECT_EQ(Mercury::os::UserPath("examples/game.mproj"),
            fs::path("examples/game.mproj"));
  EXPECT_EQ(Mercury::os::UserPath("examples/none.mproj"),
            fs::path("examples/none.mproj"));
}

TEST(UserPathTest, AbsoluteAndEmptyPathsAreUnchanged) {
  const fs::path absolute = fs::temp_directory_path() / "game.mproj";
  EXPECT_EQ(Mercury::os::UserPath(absolute), absolute);
  EXPECT_EQ(Mercury::os::UserPath(""), fs::path());
}

}  // namespace
