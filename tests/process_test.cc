// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Environment overrides for child processes. The editor uses this to set
// MTL_HUD_ENABLED for a Play session without affecting its own environment.

#include <gtest/gtest.h>

#if !defined(_WIN32)

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "os/process.h"

namespace {

TEST(ProcessTest, EnvironmentOverridesAreAppliedToTheChildOnly) {
  const char* name = "MERCURY_PROCESS_TEST_ENV";
  unsetenv(name);

  Mercury::os::Process process;
  std::string error;
  ASSERT_TRUE(process.Start(
      {"/bin/sh", "-c", "printf '%s' \"$MERCURY_PROCESS_TEST_ENV\""}, {},
      &error, {std::string(name) + "=metal-hud"}))
      << error;

  for (int i = 0; i < 500 && process.IsRunning(); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_FALSE(process.IsRunning());

  const std::vector<std::string> lines = process.TakeOutput();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "metal-hud");
  EXPECT_EQ(std::getenv(name), nullptr);
}

}  // namespace

#endif  // !defined(_WIN32)
