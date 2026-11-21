// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the player command line, in particular the hand-off of unknown
// options to a game's app hooks.

#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "player/player.h"

using namespace Mercury;

namespace {

std::vector<char*> Argv(std::initializer_list<const char*> args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const char* arg : args) argv.push_back(const_cast<char*>(arg));
  return argv;
}

PlayerArgs Parse(std::initializer_list<const char*> args, bool allow_unknown) {
  std::vector<char*> argv = Argv(args);
  return ParsePlayerArgs(static_cast<int>(argv.size()), argv.data(),
                         allow_unknown);
}

}  // namespace

TEST(PlayerArgs, ParsesEngineOptions) {
  const PlayerArgs args =
      Parse({"player", "proj.mproj", "--map", "res://maps/other.mmap",
             "--dump-schema", "out.mschema", "--build-scripts"},
            false);
  EXPECT_TRUE(args.project_given);
  EXPECT_EQ(std::filesystem::path(args.project_path).filename(), "proj.mproj");
  EXPECT_EQ(args.map_path, "res://maps/other.mmap");
  EXPECT_EQ(args.schema_path, "out.mschema");
  EXPECT_TRUE(args.build_scripts);
  EXPECT_FALSE(args.help);
  EXPECT_TRUE(args.unknown_option.empty());
}

TEST(PlayerArgs, ParsesHelp) {
  EXPECT_TRUE(Parse({"player", "--help"}, false).help);
  EXPECT_TRUE(Parse({"player", "-h"}, false).help);
}

TEST(PlayerArgs, RejectsUnknownOptionsWithoutHooks) {
  const PlayerArgs args = Parse({"player", "--server", "7777"}, false);
  EXPECT_EQ(args.unknown_option, "--server");
  EXPECT_FALSE(args.project_given);
}

TEST(PlayerArgs, AllowsUnknownOptionsForGameHooks) {
  const PlayerArgs args =
      Parse({"player", "--server", "7777", "--map", "res://maps/x.mmap"}, true);
  EXPECT_TRUE(args.unknown_option.empty());
  EXPECT_EQ(args.map_path, "res://maps/x.mmap");
  // "7777" is the value of --server, not the project path.
  EXPECT_FALSE(args.project_given);
}

TEST(PlayerArgs, ProjectBeforeGameOptionsIsStillFound) {
  const PlayerArgs args =
      Parse({"player", "proj.mproj", "--server", "7777"}, true);
  EXPECT_TRUE(args.project_given);
  EXPECT_EQ(std::filesystem::path(args.project_path).filename(), "proj.mproj");
  EXPECT_TRUE(args.unknown_option.empty());
}

TEST(PlayerArgs, ANonOptionAfterUnknownGameArgsIsNotAProject) {
  const PlayerArgs args = Parse(
      {"player", "proj.mproj", "--connect", "host", "--name", "player"}, true);
  EXPECT_TRUE(args.project_given);
  EXPECT_EQ(std::filesystem::path(args.project_path).filename(), "proj.mproj");
  EXPECT_TRUE(args.unknown_option.empty());
}
