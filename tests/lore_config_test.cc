// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Tests of the .lore/config.toml handling: rewriting the remote URL leaves
// every other key, table and comment in the file byte for byte untouched.

#include "lore_vcs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include "console.h"
#include "editor_settings.h"
#include "os/filesystem.h"
#include "project/project.h"

namespace fs = std::filesystem;
using namespace Mercury;

namespace {

std::error_code RemoveAll(const fs::path& dir) {
  std::error_code ec;
  fs::remove_all(dir, ec);
  return ec;
}

std::string Read(const fs::path& file) {
  const auto text = os::ReadText(file);
  return text ? *text : "<unreadable>";
}

}  // namespace

// A project the LoreVcs can attach to. The .lore folder is created after the
// project is attached, so no lore command is ever queued (LoreVcs only
// refreshes real repositories) and the tests never talk to a lore server.
class LoreConfig : public ::testing::Test {
 protected:
  void SetUp() override {
    root = fs::temp_directory_path() /
           ("mercury_lore_config_" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    project = Project::Create(root / "lore.mproj", "Lore Config Test");
    ASSERT_TRUE(project);
    lore = std::make_unique<LoreVcs>(console, settings);
    lore->SetProject(project);
  }

  void TearDown() override {
    lore.reset();
    RemoveAll(root);
  }

  fs::path Config() const { return root / ".lore" / "config.toml"; }

  fs::path root;
  std::shared_ptr<Project> project;
  Console console;
  EditorSettings settings;
  std::unique_ptr<LoreVcs> lore;
};

TEST_F(LoreConfig, RewriteKeepsEveryOtherLine) {
  fs::create_directories(root / ".lore");
  const std::string original =
      "# Lore repository config\n"
      "remote_url = \"https://example.com/repo.git\"\n"
      "author = \"Jane Doe\"\n"
      "\n"
      "[server]\n"
      "port = 7\n"
      "enabled = true  # keep this comment\n";
  ASSERT_TRUE(os::WriteFileAtomic(Config(), original));

  EXPECT_EQ(lore->ReadRemoteUrl(), "https://example.com/repo.git");

  const std::string escaped = "https://example.com/new \"quoted\"\\repo.git";
  ASSERT_TRUE(lore->WriteRemoteUrl(escaped));
  EXPECT_EQ(lore->ReadRemoteUrl(), escaped);

  EXPECT_EQ(Read(Config()),
            "# Lore repository config\n"
            "remote_url = \"https://example.com/new \\\"quoted\\\"\\\\"
            "repo.git\"\n"
            "author = \"Jane Doe\"\n"
            "\n"
            "[server]\n"
            "port = 7\n"
            "enabled = true  # keep this comment\n");
}

TEST_F(LoreConfig, RewritesAUrlWithNonAsciiCharacters) {
  // The value spans more bytes than codepoints, so the rewrite must count
  // columns the way the TOML parser does.
  fs::create_directories(root / ".lore");
  const std::string original =
      "remote_url = \"https://ex\xC3\xA4mple.com/\xC3\xBC/repo\"  # keep me\n"
      "\n"
      "[server]\n"
      "port = 7\n";
  ASSERT_TRUE(os::WriteFileAtomic(Config(), original));

  EXPECT_EQ(lore->ReadRemoteUrl(), "https://ex\xC3\xA4mple.com/\xC3\xBC/repo");

  ASSERT_TRUE(lore->WriteRemoteUrl("https://example.com/repo"));
  EXPECT_EQ(lore->ReadRemoteUrl(), "https://example.com/repo");

  EXPECT_EQ(Read(Config()),
            "remote_url = \"https://example.com/repo\"  # keep me\n"
            "\n"
            "[server]\n"
            "port = 7\n");
}

TEST_F(LoreConfig, AddsRemoteUrlAboveTheTables) {
  fs::create_directories(root / ".lore");
  ASSERT_TRUE(os::WriteFileAtomic(
      Config(), "author = \"Jane Doe\"\n\n[server]\nport = 7\n"));

  EXPECT_TRUE(lore->ReadRemoteUrl().empty());
  ASSERT_TRUE(lore->WriteRemoteUrl("https://example.com/repo.git"));
  EXPECT_EQ(lore->ReadRemoteUrl(), "https://example.com/repo.git");

  // Top-level keys come before the tables, and nothing else moved.
  EXPECT_EQ(Read(Config()),
            "remote_url = \"https://example.com/repo.git\"\n"
            "author = \"Jane Doe\"\n"
            "\n"
            "[server]\n"
            "port = 7\n");
}

TEST_F(LoreConfig, RefusesUrlsWithLineBreaksAndLeavesTheFileAlone) {
  fs::create_directories(root / ".lore");
  const std::string original =
      "remote_url = \"https://example.com/repo.git\"\n";
  ASSERT_TRUE(os::WriteFileAtomic(Config(), original));

  EXPECT_FALSE(lore->WriteRemoteUrl("https://example.com/bad\r\nurl"));
  EXPECT_EQ(Read(Config()), original);
  EXPECT_EQ(lore->ReadRemoteUrl(), "https://example.com/repo.git");
}

TEST_F(LoreConfig, MissingConfigReadsEmptyAndWrites) {
  EXPECT_TRUE(lore->ReadRemoteUrl().empty());
  EXPECT_TRUE(lore->WriteRemoteUrl("https://example.com/repo.git"));
  EXPECT_EQ(Read(Config()), "remote_url = \"https://example.com/repo.git\"\n");
  EXPECT_EQ(lore->ReadRemoteUrl(), "https://example.com/repo.git");
}
