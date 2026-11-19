// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pack/mpack.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "os/filesystem.h"
#include "project/asset_database.h"
#include "project/map.h"
#include "project/project.h"
#include "properties/model.h"
#include "stb/stb_image_write.h"

using namespace Mercury;

namespace {

std::vector<uint8_t> Bytes(std::string_view text) {
  return {text.begin(), text.end()};
}

std::vector<uint8_t> Noise(size_t size, unsigned seed) {
  std::mt19937 random(seed);
  std::vector<uint8_t> out(size);
  for (auto& byte : out) byte = static_cast<uint8_t>(random());
  return out;
}

class MPackTest : public testing::Test {
 protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("mpack_test_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir_);
    // Project::Load canonicalizes, so mount at the same spelling.
    dir_ = std::filesystem::canonical(dir_);
  }
  void TearDown() override {
    os::UnmountAll();
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  std::filesystem::path dir_;
};

}  // namespace

TEST_F(MPackTest, RoundTripsStoredAndCompressedEntries) {
  const std::string text(4096, 'a');
  const auto noise = Noise(4096, 1);
  mpack::Writer writer;
  writer.AddBytes("maps/main.mmap", Bytes(text));
  writer.AddBytes("assets/noise.bin", noise);
  writer.AddBytes("empty.txt", {});
  std::string error;
  ASSERT_TRUE(
      writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error))
      << error;

  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  ASSERT_EQ(reader->GetEntries().size(), 3u);
  EXPECT_EQ(reader->Find("maps/main.mmap")->compression,
            mpack::Compression::kZlib);
  EXPECT_EQ(reader->Find("assets/noise.bin")->compression,
            mpack::Compression::kStored);

  EXPECT_EQ(reader->Read("maps/main.mmap"), Bytes(text));
  EXPECT_EQ(reader->Read("assets/noise.bin"), noise);
  EXPECT_EQ(reader->Read("empty.txt"), std::vector<uint8_t>{});
  EXPECT_FALSE(reader->Read("missing.txt"));
  // The small, compressible text makes the pack smaller than its contents.
  EXPECT_LT(std::filesystem::file_size(dir_ / "game.mpack"), 2 * 4096u);
}

TEST_F(MPackTest, RollsOverIntoNumberedArchives) {
  mpack::Writer writer;
  for (int i = 0; i < 5; i++) {
    writer.AddBytes("file" + std::to_string(i), Noise(1000, i));
  }
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", 2500, &error)) << error;
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game.mpack"));
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game_001.mpack"));
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game_002.mpack"));
  EXPECT_FALSE(std::filesystem::exists(dir_ / "game_003.mpack"));

  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(reader->Read("file" + std::to_string(i)), Noise(1000, i)) << i;
  }
  // Only archive 0 can be opened as a pack.
  EXPECT_FALSE(mpack::Reader::Open(dir_ / "game_001.mpack"));
}

TEST_F(MPackTest, ZeroMaxSizeKeepsOneArchive) {
  mpack::Writer writer;
  for (int i = 0; i < 5; i++) {
    writer.AddBytes("file" + std::to_string(i), Noise(1000, i));
  }
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", mpack::kUnlimitedArchiveBytes, &error))
      << error;
  EXPECT_FALSE(std::filesystem::exists(dir_ / "game_001.mpack"));
  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  EXPECT_EQ(reader->Read("file4"), Noise(1000, 4));
}

TEST_F(MPackTest, IndexFileLooksUpManyPaths) {
  mpack::Writer writer;
  for (int i = 0; i < 500; i++) {
    writer.AddBytes("dir/file" + std::to_string(i), Noise(8, i));
  }
  std::string error;
  ASSERT_TRUE(writer.Write(dir_, "game", 1024, &error)) << error;
  EXPECT_TRUE(std::filesystem::exists(dir_ / "game.mindex"));

  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;
  ASSERT_EQ(reader->GetEntries().size(), 500u);
  for (int i = 0; i < 500; i++) {
    EXPECT_EQ(reader->Read("dir/file" + std::to_string(i)), Noise(8, i)) << i;
  }
  EXPECT_FALSE(reader->Contains("dir/file500"));
  EXPECT_FALSE(reader->Contains("dir"));
}

TEST_F(MPackTest, MissingIndexFailsToOpen) {
  mpack::Writer writer;
  writer.AddBytes("a", Noise(8, 1));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  std::filesystem::remove(dir_ / "game.mindex");
  EXPECT_FALSE(mpack::Reader::Open(dir_ / "game.mpack"));
}

TEST_F(MPackTest, DetectsCorruptData) {
  mpack::Writer writer;
  writer.AddBytes("noise.bin", Noise(256, 7));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  const uint64_t offset = reader->Find("noise.bin")->offset;
  reader.reset();

  {
    std::fstream file(dir_ / "game.mpack",
                      std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(static_cast<std::streamoff>(offset + 10));
    file.put(static_cast<char>(Noise(256, 7)[10] ^ 0x5a));
  }
  reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  std::string error;
  EXPECT_FALSE(reader->Read("noise.bin", &error));
  EXPECT_NE(error.find("checksum"), std::string::npos) << error;
}

namespace {

// Overwrites bytes of a file in place (index layout: see mpack.cc).
void Patch(const std::filesystem::path& file, std::streamoff offset,
           const void* bytes, size_t size) {
  std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out);
  out.seekp(offset);
  out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
}

}  // namespace

// A hostile index whose buckets are all occupied used to make Find spin
// forever on any path it did not hold.
TEST_F(MPackTest, IndexWithNoEmptyBucketIsRejected) {
  mpack::Writer writer;
  writer.AddBytes("a", Noise(8, 1));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  const auto index = dir_ / "game.mindex";
  uint32_t bucket_count = 0;
  {
    std::ifstream in(index, std::ios::binary);
    in.seekg(16);
    in.read(reinterpret_cast<char*>(&bucket_count), 4);
  }
  ASSERT_GT(bucket_count, 1u);
  const uint32_t one = 1;
  for (uint32_t i = 0; i < bucket_count; i++) Patch(index, 32 + i * 4, &one, 4);
  std::string error;
  EXPECT_FALSE(mpack::Reader::Open(dir_ / "game.mpack", &error));
}

// strings_size is read from the file; adding it to the other sizes must not be
// able to wrap around to the real file size and slip a huge bucket table past
// the check (which then tried to allocate it).
TEST_F(MPackTest, IndexSizeArithmeticDoesNotWrap) {
  mpack::Writer writer;
  writer.AddBytes("a", Noise(8, 1));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  const auto index = dir_ / "game.mindex";
  const uint64_t file_size = std::filesystem::file_size(index);
  const uint32_t bucket_count = 1u << 31;
  const uint32_t entry_count = 1;
  const uint64_t records = uint64_t{entry_count} * 48;
  const uint64_t table = uint64_t{bucket_count} * 4;
  const uint64_t wrapped = file_size - 32 - table - records;  // wraps mod 2^64
  Patch(index, 16, &bucket_count, 4);
  Patch(index, 24, &wrapped, 8);
  std::string error;
  EXPECT_FALSE(mpack::Reader::Open(dir_ / "game.mpack", &error));
  // Rejected by the size check, before a table that large is allocated.
  EXPECT_NE(error.find("truncated"), std::string::npos) << error;
}

// The index is untrusted: an entry that claims more bytes than its archive
// holds must fail the read instead of sizing a buffer from the claim.
TEST_F(MPackTest, EntryOutsideItsArchiveFailsToRead) {
  mpack::Writer writer;
  writer.AddBytes("noise.bin", Noise(256, 7));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  reader.reset();
  const auto index = dir_ / "game.mindex";
  uint32_t bucket_count = 0;
  {
    std::ifstream in(index, std::ios::binary);
    in.seekg(16);
    in.read(reinterpret_cast<char*>(&bucket_count), 4);
  }
  // First (only) record: stored_size is at +24 and size at +32.
  const std::streamoff record = 32 + std::streamoff{bucket_count} * 4;
  const uint64_t huge = 1ull << 30;
  Patch(index, record + 24, &huge, 8);
  Patch(index, record + 32, &huge, 8);
  reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  std::string error;
  EXPECT_FALSE(reader->Read("noise.bin", &error));
  EXPECT_NE(error.find("outside"), std::string::npos) << error;
}

// A compressed entry may not claim more than zlib can produce from its stored
// bytes, nor more than its stream really holds.
TEST_F(MPackTest, CompressedEntryWithAFalseSizeFailsToRead) {
  mpack::Writer writer;
  writer.AddBytes("text.txt", Bytes(std::string(4096, 'a')));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  const auto index = dir_ / "game.mindex";
  uint32_t bucket_count = 0;
  {
    std::ifstream in(index, std::ios::binary);
    in.seekg(16);
    in.read(reinterpret_cast<char*>(&bucket_count), 4);
  }
  const std::streamoff record = 32 + std::streamoff{bucket_count} * 4;
  for (uint64_t claimed : {uint64_t{1} << 31, uint64_t{4096 + 1}}) {
    Patch(index, record + 32, &claimed, 8);
    auto reader = mpack::Reader::Open(dir_ / "game.mpack");
    ASSERT_TRUE(reader);
    std::string error;
    EXPECT_FALSE(reader->Read("text.txt", &error)) << claimed;
    EXPECT_FALSE(error.empty());
  }
}

TEST_F(MPackTest, MissingDataArchiveFailsToRead) {
  mpack::Writer writer;
  for (int i = 0; i < 8; i++) {
    writer.AddBytes("file" + std::to_string(i), Noise(2048, i));
  }
  ASSERT_TRUE(writer.Write(dir_, "game", 4096));
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);
  std::filesystem::remove(dir_ / "game_001.mpack");
  std::string error;
  bool failed = false;
  for (int i = 0; i < 8; i++) {
    if (!reader->Read("file" + std::to_string(i), &error)) failed = true;
  }
  EXPECT_TRUE(failed);
  EXPECT_NE(error.find("could not read"), std::string::npos) << error;
}

TEST_F(MPackTest, RejectsUnsafePaths) {
  EXPECT_TRUE(mpack::IsValidEntryPath("maps/main.mmap"));
  EXPECT_FALSE(mpack::IsValidEntryPath(""));
  EXPECT_FALSE(mpack::IsValidEntryPath("/etc/passwd"));
  EXPECT_FALSE(mpack::IsValidEntryPath("../secret"));
  EXPECT_FALSE(mpack::IsValidEntryPath("a//b"));
  EXPECT_FALSE(mpack::IsValidEntryPath("a\\b"));
  EXPECT_FALSE(mpack::IsValidEntryPath("C:/x"));

  mpack::Writer writer;
  writer.AddBytes("../escape", Bytes("x"));
  std::string error;
  EXPECT_FALSE(
      writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error));
}

TEST_F(MPackTest, MountServesPathsUnderItsPrefixAndFallsBackToDisk) {
  mpack::Writer writer;
  writer.AddBytes("a.txt", Bytes("packed"));
  writer.AddBytes("ui/fonts/f.ttf", Bytes("font"));
  ASSERT_TRUE(writer.Write(dir_, "game"));
  {
    std::ofstream(dir_ / "loose.txt") << "on disk";
  }
  auto reader = mpack::Reader::Open(dir_ / "game.mpack");
  ASSERT_TRUE(reader);

  // The prefix doesn't have to exist on disk.
  const std::filesystem::path root = "/nowhere/game";
  os::Mount(root, reader);
  os::Mount("/nowhere/engine/resources", reader, "ui/");

  EXPECT_EQ(os::ReadText(root / "a.txt"), "packed");
  EXPECT_EQ(os::ReadText(root / "sub/../a.txt"), "packed");
  EXPECT_TRUE(os::IsPacked(root / "a.txt"));
  EXPECT_EQ(os::ReadText("/nowhere/engine/resources/fonts/f.ttf"), "font");
  EXPECT_FALSE(os::ReadText(root / "missing.txt"));
  EXPECT_FALSE(os::IsPacked("/nowhere/gameplay/a.txt"));

  EXPECT_EQ(os::ReadText(dir_ / "loose.txt"), "on disk");
  EXPECT_FALSE(os::IsPacked(dir_ / "loose.txt"));
}

TEST_F(MPackTest, LoadsAProjectFromAPack) {
  const auto source =
      std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/monkey_ball";
  mpack::Writer writer;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source)) {
    const auto rel = entry.path().lexically_relative(source);
    if (!entry.is_regular_file() || rel.begin()->string().starts_with('.') ||
        *rel.begin() == "scripts") {
      continue;
    }
    writer.Add(rel.generic_string(), entry.path());
  }
  std::string error;
  ASSERT_TRUE(
      writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error))
      << error;
  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;

  const std::filesystem::path root = dir_ / "shipped";
  os::Mount(root, reader);
  auto project = Project::Load(root / "monkey_ball.mproj", &error);
  ASSERT_TRUE(project) << error;
  EXPECT_EQ(project->GetRoot(), root);

  Map map;
  ASSERT_TRUE(map.Load(project->Resolve(project->GetMainMap()), &error))
      << error;
  EXPECT_TRUE(map.GetRoot());

  AssetDatabase assets(project);
  EXPECT_TRUE(assets.Load("res://assets/player.masset"));
}

// A shipped game has no loose files: a model's textures, spelled with
// Windows separators and the wrong case as OBJ files often are, must still be
// found in the pack.
TEST_F(MPackTest, ModelFindsItsTexturesInAPack) {
  const uint8_t pixels[4 * 4 * 4] = {};
  ASSERT_TRUE(stbi_write_png((dir_ / "stone.png").string().c_str(), 4, 4, 4,
                             pixels, 16));
  mpack::Writer writer;
  writer.AddBytes(
      "models/box/box.obj",
      Bytes("mtllib box.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\n"
            "vt 0 1\nusemtl Stone\nf 1/1 2/2 3/3\nusemtl Brick\nf 1/1 2/2 "
            "3/3\n"));
  writer.AddBytes("models/box/box.mtl",
                  Bytes("newmtl Stone\nKd 1 1 1\nmap_Kd textures\\Stone.PNG\n"
                        "newmtl Brick\nKd 1 1 1\nmap_Kd textures/stone.png\n"));
  writer.Add("models/box/textures/stone.png", dir_ / "stone.png");
  std::string error;
  ASSERT_TRUE(
      writer.Write(dir_, "game", mpack::kDefaultMaxArchiveBytes, &error))
      << error;
  auto reader = mpack::Reader::Open(dir_ / "game.mpack", &error);
  ASSERT_TRUE(reader) << error;

  const std::filesystem::path root = dir_ / "shipped";
  os::Mount(root, reader);
  EXPECT_EQ(os::ListDirectory(root / "models/box/textures"),
            std::vector<std::string>{"stone.png"});
  auto model = Model::Load((root / "models/box/box.obj").string(),
                           ModelImportSettings{}, &error);
  ASSERT_TRUE(model) << error;
  int textured = 0;
  for (const auto& material : model->Materials()) {
    if (material.name != "Stone" && material.name != "Brick") continue;
    ASSERT_TRUE(material.texture) << material.name;
    EXPECT_EQ(material.texture->width, 4);
    textured++;
  }
  EXPECT_EQ(textured, 2);
}

// Dropping a file into the project must not replace what is there nor write
// through a symlink to somewhere else.
TEST_F(MPackTest, CopyIntoProjectNeverOverwritesOrFollowsLinks) {
  const auto source_dir = dir_ / "outside";
  const auto dest_dir = dir_ / "assets";
  std::filesystem::create_directories(source_dir);
  std::filesystem::create_directories(dest_dir);
  const auto write = [](const std::filesystem::path& file, const char* text) {
    std::ofstream(file) << text;
  };
  const auto read = [](const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    return text;
  };
  write(source_dir / "hero.png", "new");
  write(dest_dir / "hero.png", "old");

  std::error_code ec;
  auto copy = os::CopyFileWithoutOverwriting(source_dir / "hero.png", dest_dir, ec);
  ASSERT_FALSE(ec) << ec.message();
  EXPECT_EQ(copy.filename(), "hero (1).png");
  EXPECT_EQ(read(dest_dir / "hero.png"), "old");
  EXPECT_EQ(read(copy), "new");

  // A symlink (even a dangling one) in the way is left alone too.
  const auto victim = dir_ / "victim.txt";
  write(source_dir / "link.txt", "payload");
  std::filesystem::create_symlink(victim, dest_dir / "link.txt", ec);
  ASSERT_FALSE(ec) << ec.message();
  copy = os::CopyFileWithoutOverwriting(source_dir / "link.txt", dest_dir, ec);
  ASSERT_FALSE(ec) << ec.message();
  EXPECT_EQ(copy.filename(), "link (1).txt");
  EXPECT_FALSE(std::filesystem::exists(victim));

  // The same file dropped twice reuses the copy already there.
  copy = os::CopyFileWithoutOverwriting(source_dir / "hero.png", dest_dir, ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(copy.filename(), "hero (1).png");

  // No clash: the name is kept.
  write(source_dir / "fresh.txt", "x");
  copy = os::CopyFileWithoutOverwriting(source_dir / "fresh.txt", dest_dir, ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(copy.filename(), "fresh.txt");
}
