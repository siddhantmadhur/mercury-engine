/*
 * vfs_io_system.h
 *
 * Lets Assimp read models (and the files they reference, like a glTF's .bin
 * or an OBJ's .mtl) through os::ReadBytes, so they can come from a mounted
 * .mpack.
 */
#ifndef MERCURY_PROPERTIES_VFS_IO_SYSTEM_H
#define MERCURY_PROPERTIES_VFS_IO_SYSTEM_H

#include <algorithm>
#include <cstring>
#include <vector>

#include "assimp/IOStream.hpp"
#include "assimp/IOSystem.hpp"
#include "assimp/Importer.hpp"
#include "os/filesystem.h"

namespace Mercury {

class VfsIOStream : public Assimp::IOStream {
 public:
  explicit VfsIOStream(std::vector<uint8_t> data) : data_(std::move(data)) {}

  size_t Read(void* buffer, size_t size, size_t count) override {
    if (size == 0) return 0;
    const size_t items = std::min(count, (data_.size() - pos_) / size);
    std::memcpy(buffer, data_.data() + pos_, items * size);
    pos_ += items * size;
    return items;
  }
  size_t Write(const void*, size_t, size_t) override { return 0; }
  aiReturn Seek(size_t offset, aiOrigin origin) override {
    size_t target = offset;
    if (origin == aiOrigin_CUR) target = pos_ + offset;
    if (origin == aiOrigin_END) target = data_.size() - offset;
    if (target > data_.size()) return aiReturn_FAILURE;
    pos_ = target;
    return aiReturn_SUCCESS;
  }
  size_t Tell() const override { return pos_; }
  size_t FileSize() const override { return data_.size(); }
  void Flush() override {}

 private:
  std::vector<uint8_t> data_;
  size_t pos_ = 0;
};

class VfsIOSystem : public Assimp::IOSystem {
 public:
  bool Exists(const char* file) const override { return os::Exists(file); }
  char getOsSeparator() const override { return '/'; }
  Assimp::IOStream* Open(const char* file, const char* mode) override {
    if (std::strchr(mode, 'w') || std::strchr(mode, 'a')) return nullptr;
    auto data = os::ReadBytes(file);
    return data ? new VfsIOStream(std::move(*data)) : nullptr;
  }
  void Close(Assimp::IOStream* file) override { delete file; }
};

// Makes `importer` read through the VFS when a pack is mounted. Without one
// Assimp keeps its own file access.
inline void UseVfs(Assimp::Importer& importer) {
  if (os::HasMounts()) importer.SetIOHandler(new VfsIOSystem());
}

}  // namespace Mercury

#endif  // MERCURY_PROPERTIES_VFS_IO_SYSTEM_H
