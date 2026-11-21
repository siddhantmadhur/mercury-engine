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

TEST(PlayerArgs, ParsesEngineSchemaPath) {
  EXPECT_EQ(Parse({"player", "--dump-engine-schema", "out.json"}, false)
                .engine_schema_path,
            "out.json");
  // "-" and a missing value both mean stdout.
  EXPECT_EQ(
      Parse({"player", "--dump-engine-schema", "-"}, false).engine_schema_path,
      "-");
  EXPECT_EQ(
      Parse({"player", "--dump-engine-schema"}, false).engine_schema_path,
      "-");
}

// A following option is not a file name: the schema goes to stdout and the
// option still applies.
TEST(PlayerArgs, EngineSchemaDoesNotSwallowOptions) {
  const PlayerArgs args =
      Parse({"player", "--dump-engine-schema", "--validate"}, false);
  EXPECT_EQ(args.engine_schema_path, "-");
  EXPECT_TRUE(args.validate);
  const PlayerArgs with_value =
      Parse({"player", "--dump-engine-schema", "--validate", "out.json"},
            false);
  EXPECT_EQ(with_value.engine_schema_path, "-");
  EXPECT_TRUE(with_value.validate);
  // The token after the option becomes the project path, as usual.
  EXPECT_TRUE(with_value.project_given);
}

TEST(PlayerArgs, ParsesValidateAndJson) {
  const PlayerArgs args = Parse({"player", "--validate", "--json"}, false);
  EXPECT_TRUE(args.validate);
  EXPECT_TRUE(args.json);
  EXPECT_FALSE(Parse({"player", "--validate"}, false).json);
}

TEST(PlayerArgs, ParsesInspectAsset) {
  const PlayerArgs args =
      Parse({"player", "--inspect-asset", "res://assets/player.masset"},
            false);
  EXPECT_EQ(args.inspect_asset, "res://assets/player.masset");
}

TEST(PlayerArgs, InspectAssetRequiresAValue) {
  const PlayerArgs args = Parse({"player", "--inspect-asset"}, false);
  EXPECT_FALSE(args.option_error.empty());
}

TEST(PlayerArgs, ParsesHeadlessAndFrames) {
  const PlayerArgs args =
      Parse({"player", "--headless", "--frames", "30"}, false);
  EXPECT_TRUE(args.headless);
  EXPECT_EQ(args.frames, 30);
}

TEST(PlayerArgs, FramesRejectsANonCount) {
  EXPECT_FALSE(Parse({"player", "--frames", "abc"}, false).option_error.empty());
  EXPECT_FALSE(Parse({"player", "--frames", "-5"}, false).option_error.empty());
  EXPECT_FALSE(Parse({"player", "--frames"}, false).option_error.empty());
  EXPECT_EQ(Parse({"player", "--frames", "abc"}, false).frames, 0);
}

TEST(PlayerArgs, FramesIsKnownEvenForGameHooks) {
  const PlayerArgs args =
      Parse({"player", "--frames", "12", "--server", "7777"}, true);
  EXPECT_EQ(args.frames, 12);
  EXPECT_TRUE(args.unknown_option.empty());
  EXPECT_FALSE(args.project_given);
}

// The whole validate path, not just the parsing: load the example, build its
// scripts and run the introspection pass. The example must validate clean:
// exit 0 with no errors. Warnings (e.g. unknown types) are allowed and do not
// change the exit code.
TEST(PlayerArgs, ValidateRunsTheExampleProjectClean) {
  std::string program = "player";
  std::string project = std::string(MERCURY_SOURCE_DIR) +
                        "/examples/roll_a_ball/roll_a_ball.mproj";
  std::string validate = "--validate";
  char* argv[] = {program.data(), project.data(), validate.data()};
  EXPECT_EQ(RunPlayer(3, argv, nullptr), 0);
}

// --json without --validate is rejected with exit 2, not silently ignored.
TEST(PlayerArgs, JsonWithoutValidateIsRejected) {
  std::string program = "player";
  std::string json = "--json";
  char* argv[] = {program.data(), json.data()};
  EXPECT_EQ(RunPlayer(2, argv, nullptr), 2);
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
