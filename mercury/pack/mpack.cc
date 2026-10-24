#include "mpack.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <format>
#include <system_error>

namespace Mercury::mpack {

namespace {

constexpr char kMagic[4] = {'M', 'P', 'A', 'K'};
constexpr char kIndexMagic[4] = {'M', 'I', 'D', 'X'};
constexpr size_t kHeaderSize = 32;
constexpr size_t kIndexHeaderSize = 32;
constexpr size_t kIndexEntrySize = 48;
// Load factor of the index's hash table is at most 1/2.
constexpr uint32_t kMinBuckets = 16;
constexpr uint64_t kAlignment = 16;
// Compressed bytes are only kept when they save at least this much, so
// PNGs, JPEGs and fonts are stored as they are.
constexpr double kMinCompressionRatio = 0.95;

struct Header {
  uint32_t version = kVersion;
  uint16_t archive_index = 0;
  uint16_t archive_count = 0;
};

class ByteWriter {
 public:
  void U8(uint8_t value) { bytes_.push_back(value); }
  void U16(uint16_t value) { Le(value, 2); }
  void U32(uint32_t value) { Le(value, 4); }
  void U64(uint64_t value) { Le(value, 8); }
  void Raw(const void* data, size_t size) {
    auto* p = static_cast<const uint8_t*>(data);
    bytes_.insert(bytes_.end(), p, p + size);
  }
  const std::vector<uint8_t>& Bytes() const { return bytes_; }

 private:
  void Le(uint64_t value, int size) {
    for (int i = 0; i < size; i++) bytes_.push_back((value >> (8 * i)) & 0xFF);
  }
  std::vector<uint8_t> bytes_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool Ok() const { return ok_; }
  uint8_t U8() { return static_cast<uint8_t>(Le(1)); }
  uint16_t U16() { return static_cast<uint16_t>(Le(2)); }
  uint32_t U32() { return static_cast<uint32_t>(Le(4)); }
  uint64_t U64() { return Le(8); }
  std::string String(size_t size) {
    if (!Need(size)) return {};
    std::string out(reinterpret_cast<const char*>(data_ + pos_), size);
    pos_ += size;
    return out;
  }
  bool Magic(const char (&magic)[4] = kMagic) {
    if (!Need(4)) return false;
    bool match = std::memcmp(data_ + pos_, magic, 4) == 0;
    pos_ += 4;
    return match;
  }

 private:
  bool Need(size_t size) {
    if (!ok_ || size > size_ - pos_) ok_ = false;
    return ok_;
  }
  uint64_t Le(int size) {
    if (!Need(size)) return 0;
    uint64_t value = 0;
    for (int i = 0; i < size; i++) {
      value |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
    }
    pos_ += size;
    return value;
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  bool ok_ = true;
};

std::vector<uint8_t> EncodeHeader(const Header& header) {
  ByteWriter out;
  out.Raw(kMagic, 4);
  out.U32(header.version);
  out.U16(header.archive_index);
  out.U16(header.archive_count);
  for (int i = 0; i < 16; i++) out.U8(0);
  return out.Bytes();
}

}  // namespace

uint64_t HashPath(std::string_view path) {
  uint64_t hash = 14695981039346656037ull;  // FNV-1a
  for (const char c : path) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

namespace {

uint32_t BucketCount(size_t entries) {
  uint32_t buckets = kMinBuckets;
  while (buckets < entries * 2) buckets *= 2;
  return buckets;
}

uint32_t Crc32(const std::vector<uint8_t>& data) {
  uLong crc = crc32(0L, Z_NULL, 0);
  size_t done = 0;
  while (done < data.size()) {
    const auto chunk =
        static_cast<uInt>(std::min<size_t>(data.size() - done, UINT_MAX));
    crc = crc32(crc, data.data() + done, chunk);
    done += chunk;
  }
  return static_cast<uint32_t>(crc);
}

std::optional<std::vector<uint8_t>> Compress(const std::vector<uint8_t>& data) {
  if (data.empty() || data.size() > UINT32_MAX) return std::nullopt;
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  std::vector<uint8_t> out(size);
  if (compress2(out.data(), &size, data.data(), static_cast<uLong>(data.size()),
                Z_BEST_COMPRESSION) != Z_OK) {
    return std::nullopt;
  }
  out.resize(size);
  return out;
}

std::optional<std::vector<uint8_t>> ReadWholeFile(
    const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return std::nullopt;
  const auto size = static_cast<std::streamoff>(file.tellg());
  if (size < 0) return std::nullopt;
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.seekg(0);
  if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
    return std::nullopt;
  }
  return data;
}

uint64_t AlignUp(uint64_t value) {
  return (value + kAlignment - 1) / kAlignment * kAlignment;
}

}  // namespace

std::filesystem::path IndexPath(const std::filesystem::path& dir,
                                std::string_view base_name) {
  return dir / std::format("{}{}", base_name, kIndexExtension);
}

std::filesystem::path ArchivePath(const std::filesystem::path& dir,
                                  std::string_view base_name, int index) {
  if (index == 0) return dir / std::format("{}{}", base_name, kExtension);
  return dir / std::format("{}_{:03}{}", base_name, index, kExtension);
}

bool IsValidEntryPath(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.back() == '/') return false;
  if (path.find('\\') != std::string_view::npos) return false;
  if (path.find(':') != std::string_view::npos) return false;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    const auto part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    start = end + 1;
  }
  return path.size() <= UINT16_MAX;
}

void Writer::Add(std::string path, std::filesystem::path source) {
  sources_[std::move(path)] = Source{std::move(source), std::nullopt};
}

void Writer::AddBytes(std::string path, std::vector<uint8_t> data) {
  sources_[std::move(path)] = Source{{}, std::move(data)};
}

bool Writer::Write(const std::filesystem::path& dir, std::string_view base_name,
                   uint64_t max_archive_bytes, std::string* error) const {
  auto fail = [&](std::string message) {
    if (error) *error = std::move(message);
    return false;
  };
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return fail("could not create " + dir.string() + ": " + ec.message());

  std::vector<Entry> entries;
  entries.reserve(sources_.size());
  std::ofstream out;
  std::filesystem::path out_path;
  int archive = -1;
  uint64_t pos = 0;
  bool archive_has_data = false;

  auto open_next = [&]() {
    out.close();
    archive++;
    if (archive > UINT16_MAX) return fail("too many archives");
    out_path = ArchivePath(dir, base_name, archive);
    out.open(out_path, std::ios::binary | std::ios::trunc);
    const std::vector<uint8_t> blank(kHeaderSize, 0);
    out.write(reinterpret_cast<const char*>(blank.data()), blank.size());
    if (!out) return fail("could not write " + out_path.string());
    pos = kHeaderSize;
    archive_has_data = false;
    return true;
  };
  if (!open_next()) return false;

  for (const auto& [path, source] : sources_) {
    if (!IsValidEntryPath(path)) return fail("invalid pack path \"" + path + "\"");
    std::optional<std::vector<uint8_t>> loaded;
    if (!source.bytes) {
      loaded = ReadWholeFile(source.file);
      if (!loaded) return fail("could not read " + source.file.string());
    }
    const std::vector<uint8_t>& data = source.bytes ? *source.bytes : *loaded;

    Entry entry;
    entry.path = path;
    entry.size = data.size();
    entry.crc32 = Crc32(data);
    auto compressed = Compress(data);
    const bool use_compressed =
        compressed && compressed->size() <= data.size() * kMinCompressionRatio;
    const std::vector<uint8_t>& stored = use_compressed ? *compressed : data;
    entry.compression =
        use_compressed ? Compression::kZlib : Compression::kStored;
    entry.stored_size = stored.size();

    uint64_t offset = AlignUp(pos);
    if (max_archive_bytes != 0 && archive_has_data &&
        offset + stored.size() > max_archive_bytes) {
      if (!open_next()) return false;
      offset = AlignUp(pos);
    }
    const std::vector<char> padding(offset - pos, 0);
    out.write(padding.data(), padding.size());
    out.write(reinterpret_cast<const char*>(stored.data()), stored.size());
    if (!out) return fail("could not write " + out_path.string());
    entry.archive = static_cast<uint16_t>(archive);
    entry.offset = offset;
    pos = offset + stored.size();
    archive_has_data = true;
    entries.push_back(std::move(entry));
  }
  out.close();
  const int archive_count = archive + 1;

  // The index is its own file: a hash table of path hashes, then the
  // entries, then their paths.
  ByteWriter strings;
  ByteWriter records;
  const uint32_t bucket_count = BucketCount(entries.size());
  std::vector<uint32_t> buckets(bucket_count, 0);
  for (size_t i = 0; i < entries.size(); i++) {
    const Entry& entry = entries[i];
    const uint64_t hash = HashPath(entry.path);
    for (uint32_t b = hash & (bucket_count - 1); ; b = (b + 1) & (bucket_count - 1)) {
      if (buckets[b] == 0) {
        buckets[b] = static_cast<uint32_t>(i + 1);
        break;
      }
    }
    records.U64(hash);
    records.U32(static_cast<uint32_t>(strings.Bytes().size()));
    records.U16(static_cast<uint16_t>(entry.path.size()));
    records.U16(entry.archive);
    records.U64(entry.offset);
    records.U64(entry.stored_size);
    records.U64(entry.size);
    records.U8(static_cast<uint8_t>(entry.compression));
    records.U8(0);
    records.U16(0);
    records.U32(entry.crc32);
    strings.Raw(entry.path.data(), entry.path.size());
  }
  if (strings.Bytes().size() > UINT32_MAX) return fail("too many paths");
  ByteWriter index;
  index.Raw(kIndexMagic, 4);
  index.U32(kVersion);
  index.U32(static_cast<uint32_t>(archive_count));
  index.U32(static_cast<uint32_t>(entries.size()));
  index.U32(bucket_count);
  index.U32(0);
  index.U64(strings.Bytes().size());
  for (const uint32_t bucket : buckets) index.U32(bucket);
  index.Raw(records.Bytes().data(), records.Bytes().size());
  index.Raw(strings.Bytes().data(), strings.Bytes().size());
  const auto index_path = IndexPath(dir, base_name);
  {
    std::ofstream file(index_path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(index.Bytes().data()),
               index.Bytes().size());
    if (!file) return fail("could not write " + index_path.string());
  }

  for (int i = 0; i < archive_count; i++) {
    Header header;
    header.archive_index = static_cast<uint16_t>(i);
    header.archive_count = static_cast<uint16_t>(archive_count);
    const auto path = ArchivePath(dir, base_name, i);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    const auto bytes = EncodeHeader(header);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file) return fail("could not write " + path.string());
  }
  return true;
}

std::shared_ptr<Reader> Reader::Open(const std::filesystem::path& archive0,
                                     std::string* error) {
  auto fail = [&](std::string message) -> std::shared_ptr<Reader> {
    if (error) *error = archive0.string() + ": " + message;
    return nullptr;
  };
  auto file = std::make_unique<std::ifstream>(archive0, std::ios::binary);
  if (!*file) return fail("could not open");

  uint8_t raw_header[kHeaderSize];
  if (!file->read(reinterpret_cast<char*>(raw_header), kHeaderSize)) {
    return fail("not an .mpack archive");
  }
  ByteReader header_reader(raw_header, kHeaderSize);
  if (!header_reader.Magic()) return fail("not an .mpack archive");
  Header header;
  header.version = header_reader.U32();
  header.archive_index = header_reader.U16();
  header.archive_count = header_reader.U16();
  if (header.version != kVersion) {
    return fail(std::format("unsupported version {}", header.version));
  }
  if (header.archive_index != 0) {
    return fail("is a data archive; open the pack's first .mpack file");
  }

  const auto dir = archive0.parent_path();
  const auto base_name = archive0.stem().string();
  const auto index_path = IndexPath(dir, base_name);
  auto index = ReadWholeFile(index_path);
  if (!index) return fail("could not read " + index_path.filename().string());
  ByteReader in(index->data(), index->size());
  if (!in.Magic(kIndexMagic)) return fail("index is not a .mindex file");
  const uint32_t version = in.U32();
  const uint32_t archive_count = in.U32();
  const uint32_t entry_count = in.U32();
  const uint32_t bucket_count = in.U32();
  in.U32();
  const uint64_t strings_size = in.U64();
  if (!in.Ok() || version != kVersion) return fail("unsupported index version");
  if (archive_count != header.archive_count || bucket_count == 0 ||
      (bucket_count & (bucket_count - 1)) != 0 || bucket_count <= entry_count) {
    return fail("index does not match the archive");
  }
  const uint64_t table_size = uint64_t{bucket_count} * 4;
  const uint64_t records_size = uint64_t{entry_count} * kIndexEntrySize;
  if (index->size() != kIndexHeaderSize + table_size + records_size + strings_size) {
    return fail("index is truncated");
  }

  std::shared_ptr<Reader> reader(new Reader());
  reader->dir_ = dir;
  reader->base_name_ = base_name;
  reader->buckets_.resize(bucket_count);
  for (auto& bucket : reader->buckets_) bucket = in.U32();
  const uint8_t* strings = index->data() + kIndexHeaderSize + table_size + records_size;
  for (uint32_t i = 0; i < entry_count; i++) {
    Entry entry;
    const uint64_t hash = in.U64();
    const uint32_t path_offset = in.U32();
    const uint16_t path_length = in.U16();
    entry.archive = in.U16();
    entry.offset = in.U64();
    entry.stored_size = in.U64();
    entry.size = in.U64();
    const uint8_t compression = in.U8();
    in.U8();
    in.U16();
    entry.crc32 = in.U32();
    if (!in.Ok() || path_offset > strings_size ||
        path_length > strings_size - path_offset) {
      return fail("index is corrupt");
    }
    entry.path.assign(reinterpret_cast<const char*>(strings) + path_offset,
                      path_length);
    if (compression > static_cast<uint8_t>(Compression::kZlib) ||
        entry.archive >= archive_count || !IsValidEntryPath(entry.path) ||
        HashPath(entry.path) != hash) {
      return fail("index entry \"" + entry.path + "\" is corrupt");
    }
    entry.compression = static_cast<Compression>(compression);
    reader->entries_.push_back(std::move(entry));
  }
  for (const uint32_t bucket : reader->buckets_) {
    if (bucket > entry_count) return fail("index is corrupt");
  }
  reader->archives_.resize(archive_count);
  reader->archives_[0] = std::move(file);
  return reader;
}

const Entry* Reader::Find(std::string_view path) const {
  const uint64_t mask = buckets_.size() - 1;
  for (uint64_t b = HashPath(path) & mask;; b = (b + 1) & mask) {
    const uint32_t slot = buckets_[b];
    if (slot == 0) return nullptr;
    if (entries_[slot - 1].path == path) return &entries_[slot - 1];
  }
}

bool Reader::Contains(std::string_view path) const {
  return Find(path) != nullptr;
}

std::optional<std::vector<uint8_t>> Reader::Read(std::string_view path,
                                                 std::string* error) const {
  auto fail = [&](std::string message) -> std::optional<std::vector<uint8_t>> {
    if (error) *error = std::string(path) + ": " + message;
    return std::nullopt;
  };
  const Entry* entry = Find(path);
  if (!entry) return fail("not in the pack");

  std::vector<uint8_t> stored(entry->stored_size);
  {
    std::lock_guard lock(mutex_);
    auto& archive = archives_[entry->archive];
    if (!archive) {
      archive = std::make_unique<std::ifstream>(
          ArchivePath(dir_, base_name_, entry->archive), std::ios::binary);
    }
    archive->clear();
    archive->seekg(static_cast<std::streamoff>(entry->offset));
    if (!stored.empty() &&
        !archive->read(reinterpret_cast<char*>(stored.data()), stored.size())) {
      archive->clear();
      return fail(std::format("could not read {}",
                              ArchivePath(dir_, base_name_, entry->archive)
                                  .filename()
                                  .string()));
    }
  }

  std::vector<uint8_t> data;
  if (entry->compression == Compression::kZlib) {
    if (entry->size > UINT32_MAX || entry->stored_size > UINT32_MAX) {
      return fail("entry is too large to decompress");
    }
    data.resize(entry->size);
    uLongf size = static_cast<uLongf>(entry->size);
    if (uncompress(data.data(), &size, stored.data(),
                   static_cast<uLong>(stored.size())) != Z_OK ||
        size != entry->size) {
      return fail("data is corrupt");
    }
  } else {
    data = std::move(stored);
  }
  if (Crc32(data) != entry->crc32) return fail("checksum mismatch");
  return data;
}

}  // namespace Mercury::mpack
