#include "filesystem.h"

#include <fstream>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <system_error>

#include "pack/mpack.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace Mercury::os {

namespace {

struct MountPoint {
  std::string prefix;  // normalized, generic, ends with '/'
  std::shared_ptr<const mpack::Reader> pack;
  std::string entry_prefix;
};

std::shared_mutex g_mounts_mutex;
std::vector<MountPoint> g_mounts;

std::string Normalize(const fs::path& path) {
  return path.lexically_normal().generic_string();
}

// The pack and entry serving `path`, if any.
struct Packed {
  std::shared_ptr<const mpack::Reader> pack;
  std::string entry;
};

std::optional<Packed> FindPacked(const fs::path& path) {
  std::shared_lock lock(g_mounts_mutex);
  if (g_mounts.empty()) return std::nullopt;
  const std::string key = Normalize(path);
  for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
    if (!key.starts_with(it->prefix)) continue;
    std::string entry = it->entry_prefix + key.substr(it->prefix.size());
    if (it->pack->Contains(entry)) return Packed{it->pack, std::move(entry)};
  }
  return std::nullopt;
}

}  // namespace

void Mount(const fs::path& prefix, std::shared_ptr<const mpack::Reader> pack,
           std::string entry_prefix) {
  std::string normalized = Normalize(prefix);
  if (!normalized.ends_with('/')) normalized += '/';
  std::unique_lock lock(g_mounts_mutex);
  g_mounts.push_back({std::move(normalized), std::move(pack),
                      std::move(entry_prefix)});
}

void UnmountAll() {
  std::unique_lock lock(g_mounts_mutex);
  g_mounts.clear();
}

bool HasMounts() {
  std::shared_lock lock(g_mounts_mutex);
  return !g_mounts.empty();
}

std::optional<std::vector<uint8_t>> ReadBytes(const fs::path& path) {
  if (auto packed = FindPacked(path)) return packed->pack->Read(packed->entry);
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

std::optional<std::string> ReadText(const fs::path& path) {
  auto bytes = ReadBytes(path);
  if (!bytes) return std::nullopt;
  return std::string(bytes->begin(), bytes->end());
}

bool Exists(const fs::path& path) {
  if (FindPacked(path)) return true;
  std::error_code ec;
  return fs::exists(path, ec);
}

bool IsPacked(const fs::path& path) { return FindPacked(path).has_value(); }

std::vector<std::string> ListDirectory(const fs::path& dir) {
  std::vector<std::string> names;
  std::set<std::string> seen;
  auto add = [&](std::string name) {
    if (seen.insert(name).second) names.push_back(std::move(name));
  };
  std::string key = Normalize(dir);
  if (key == ".") key.clear();
  if (!key.empty() && !key.ends_with('/')) key += '/';
  {
    std::shared_lock lock(g_mounts_mutex);
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
      if (!key.starts_with(it->prefix)) continue;
      const std::string folder = it->entry_prefix + key.substr(it->prefix.size());
      for (const mpack::Entry& entry : it->pack->GetEntries()) {
        if (!entry.path.starts_with(folder)) continue;
        std::string rest = entry.path.substr(folder.size());
        if (!rest.empty() && rest.find('/') == std::string::npos) add(std::move(rest));
      }
    }
  }
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec)) {
    if (entry.is_regular_file(ec)) add(entry.path().filename().string());
  }
  return names;
}

fs::path ExecutableDir() {
  std::error_code ec;
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
    if (size == 0) return fs::current_path(ec);
    if (size < buffer.size()) {
      buffer.resize(size);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return fs::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return fs::current_path(ec);
  }
  return fs::weakly_canonical(fs::path(buffer.c_str()), ec).parent_path();
#elif defined(__EMSCRIPTEN__)
  return fs::current_path(ec);
#else
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return fs::current_path(ec);
  return exe.parent_path();
#endif
}

}  // namespace Mercury::os
