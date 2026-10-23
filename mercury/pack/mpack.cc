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
constexpr size_t kHeaderSize = 32;
constexpr uint64_t kAlignment = 16;
// Compressed bytes are only kept when they save at least this much, so
// PNGs, JPEGs and fonts are stored as they are.
constexpr double kMinCompressionRatio = 0.95;

struct Header {
  uint32_t version = kVersion;
  uint16_t archive_index = 0;
  uint16_t archive_count = 0;
  uint32_t entry_count = 0;
  uint64_t index_offset = 0;
  uint64_t index_size = 0;
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
  bool Magic() {
    if (!Need(4)) return false;
    bool match = std::memcmp(data_ + pos_, kMagic, 4) == 0;
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
  out.U32(header.entry_count);
  out.U64(header.index_offset);
  out.U64(header.index_size);
  return out.Bytes();
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
    if (archive_has_data && offset + stored.size() > max_archive_bytes) {
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

  // The index goes at the end of archive 0.
  ByteWriter index;
  for (const auto& entry : entries) {
    index.U16(static_cast<uint16_t>(entry.path.size()));
    index.Raw(entry.path.data(), entry.path.size());
    index.U16(entry.archive);
    index.U64(entry.offset);
    index.U64(entry.stored_size);
    index.U64(entry.size);
    index.U8(static_cast<uint8_t>(entry.compression));
    index.U32(entry.crc32);
  }
  const auto first = ArchivePath(dir, base_name, 0);
  std::fstream file0(first, std::ios::binary | std::ios::in | std::ios::out);
  file0.seekp(0, std::ios::end);
  Header header;
  header.index_offset = static_cast<uint64_t>(file0.tellp());
  header.index_size = index.Bytes().size();
  header.entry_count = static_cast<uint32_t>(entries.size());
  header.archive_count = static_cast<uint16_t>(archive_count);
  file0.write(reinterpret_cast<const char*>(index.Bytes().data()),
              index.Bytes().size());
  const auto encoded = EncodeHeader(header);
  file0.seekp(0);
  file0.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
  if (!file0) return fail("could not write " + first.string());
  file0.close();

  for (int i = 1; i < archive_count; i++) {
    Header data_header;
    data_header.archive_index = static_cast<uint16_t>(i);
    data_header.archive_count = static_cast<uint16_t>(archive_count);
    const auto path = ArchivePath(dir, base_name, i);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    const auto bytes = EncodeHeader(data_header);
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
  header.entry_count = header_reader.U32();
  header.index_offset = header_reader.U64();
  header.index_size = header_reader.U64();
  if (header.version != kVersion) {
    return fail(std::format("unsupported version {}", header.version));
  }
  if (header.archive_index != 0) {
    return fail("is a data archive; open the pack's first .mpack file");
  }

  file->seekg(0, std::ios::end);
  const auto file_size = static_cast<uint64_t>(file->tellg());
  if (header.index_offset > file_size ||
      header.index_size > file_size - header.index_offset) {
    return fail("index is truncated");
  }
  std::vector<uint8_t> index(header.index_size);
  file->seekg(static_cast<std::streamoff>(header.index_offset));
  if (!index.empty() &&
      !file->read(reinterpret_cast<char*>(index.data()), index.size())) {
    return fail("could not read the index");
  }

  std::shared_ptr<Reader> reader(new Reader());
  reader->dir_ = archive0.parent_path();
  reader->base_name_ = archive0.stem().string();
  ByteReader in(index.data(), index.size());
  for (uint32_t i = 0; i < header.entry_count; i++) {
    Entry entry;
    entry.path = in.String(in.U16());
    entry.archive = in.U16();
    entry.offset = in.U64();
    entry.stored_size = in.U64();
    entry.size = in.U64();
    const uint8_t compression = in.U8();
    entry.crc32 = in.U32();
    if (!in.Ok()) return fail("index is corrupt");
    if (compression > static_cast<uint8_t>(Compression::kZlib) ||
        entry.archive >= header.archive_count ||
        !IsValidEntryPath(entry.path)) {
      return fail("index entry \"" + entry.path + "\" is corrupt");
    }
    entry.compression = static_cast<Compression>(compression);
    reader->by_path_[entry.path] = reader->entries_.size();
    reader->entries_.push_back(std::move(entry));
  }
  reader->archives_.resize(header.archive_count);
  reader->archives_[0] = std::move(file);
  return reader;
}

const Entry* Reader::Find(std::string_view path) const {
  auto it = by_path_.find(path);
  return it == by_path_.end() ? nullptr : &entries_[it->second];
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
