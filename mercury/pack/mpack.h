/*
 * mpack.h
 *
 * .mpack archives hold a shipped game's assets in a few large files instead
 * of thousands of loose ones, like Source 2's .vpk files. Only the editor's
 * Build Game writes them; development builds read loose files.
 *
 * A pack named "game" is a set of archives:
 *
 *   game.mpack       header, file data, then the index of every entry
 *   game_001.mpack   header and more file data, once game.mpack is full
 *   game_002.mpack   ...
 *
 * Every archive starts with the same 32-byte little-endian header:
 *
 *   char[4] magic "MPAK"     u32 version
 *   u16 archive_index        u16 archive_count
 *   u32 entry_count          u64 index_offset      u64 index_size
 *
 * Only archive 0 has an index (the others store 0s). Each index entry is
 *
 *   u16 path_length, path (UTF-8, '/'-separated, relative to the pack root)
 *   u16 archive   u64 offset   u64 stored_size   u64 size
 *   u8 compression (0 stored, 1 zlib)   u32 crc32 (of the uncompressed bytes)
 *
 * sorted by path.
 */
#ifndef MERCURY_PACK_MPACK_H
#define MERCURY_PACK_MPACK_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mercury::mpack {

inline constexpr std::string_view kExtension = ".mpack";
inline constexpr uint32_t kVersion = 1;
// Archives roll over to the next _NNN file past this size, as .vpk does.
inline constexpr uint64_t kDefaultMaxArchiveBytes = 1ull << 30;

// What Build Game writes next to the executable, and RunPlayer mounts.
inline constexpr std::string_view kGamePackName = "game";
inline constexpr std::string_view kEnginePackName = "engine";
// Folders inside engine.mpack.
inline constexpr std::string_view kEngineUiFolder = "ui/";
inline constexpr std::string_view kEngineShaderFolder = "shaders/";

enum class Compression : uint8_t { kStored = 0, kZlib = 1 };

struct Entry {
  std::string path;
  uint16_t archive = 0;
  uint64_t offset = 0;
  uint64_t stored_size = 0;
  uint64_t size = 0;
  Compression compression = Compression::kStored;
  uint32_t crc32 = 0;
};

// <dir>/<base>.mpack for archive 0, <dir>/<base>_NNN.mpack after that.
std::filesystem::path ArchivePath(const std::filesystem::path& dir,
                                  std::string_view base_name, int index);

// False for paths a pack can't hold: empty, absolute, or with "." or "..".
bool IsValidEntryPath(std::string_view path);

class Writer {
 public:
  // The file is read when Write() runs, one at a time.
  void Add(std::string path, std::filesystem::path source);
  void AddBytes(std::string path, std::vector<uint8_t> data);

  size_t GetEntryCount() const { return sources_.size(); }

  /**
   * @brief Writes <dir>/<base_name>.mpack and any _NNN archives it needs
   * @return false with `error` filled if a source could not be read or an
   * archive could not be written
   */
  bool Write(const std::filesystem::path& dir, std::string_view base_name,
             uint64_t max_archive_bytes = kDefaultMaxArchiveBytes,
             std::string* error = nullptr) const;

 private:
  struct Source {
    std::filesystem::path file;
    std::optional<std::vector<uint8_t>> bytes;
  };
  std::map<std::string, Source> sources_;
};

class Reader {
 public:
  // Opens archive 0 (e.g. game.mpack) and reads its index. The _NNN
  // archives next to it are opened when first needed.
  static std::shared_ptr<Reader> Open(const std::filesystem::path& archive0,
                                      std::string* error = nullptr);

  bool Contains(std::string_view path) const;
  const Entry* Find(std::string_view path) const;
  const std::vector<Entry>& GetEntries() const { return entries_; }

  // The entry's bytes, or nullopt if it is missing, unreadable or corrupt.
  // Safe to call from several threads.
  std::optional<std::vector<uint8_t>> Read(std::string_view path,
                                           std::string* error = nullptr) const;

 private:
  Reader() = default;

  std::filesystem::path dir_;
  std::string base_name_;
  std::vector<Entry> entries_;
  std::map<std::string, size_t, std::less<>> by_path_;

  mutable std::mutex mutex_;
  mutable std::vector<std::unique_ptr<std::ifstream>> archives_;
};

}  // namespace Mercury::mpack

#endif  // MERCURY_PACK_MPACK_H
