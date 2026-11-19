// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "filesystem.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <format>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <shared_mutex>
#include <system_error>

#include "pack/mpack.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
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

// A whole-file read turns the size in the file header into one allocation, so
// refuse absurd sizes instead of letting them reach std::bad_alloc.
constexpr uint64_t kMaxWholeFileSize = 2ull * 1024 * 1024 * 1024;

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
  g_mounts.push_back(
      {std::move(normalized), std::move(pack), std::move(entry_prefix)});
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
  if (static_cast<uint64_t>(size) > kMaxWholeFileSize) return std::nullopt;
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

namespace {

#if defined(_WIN32)
using FileHandle = int;
constexpr FileHandle kInvalidFile = -1;

FileHandle OpenExclusive(const fs::path& path) {
  return _wopen(path.c_str(),
                _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                _S_IREAD | _S_IWRITE);
}
bool WriteAll(FileHandle file, std::string_view data) {
  size_t written = 0;
  while (written < data.size()) {
    const auto chunk = static_cast<unsigned>(
        std::min<size_t>(data.size() - written, UINT_MAX));
    const int result = _write(file, data.data() + written, chunk);
    if (result <= 0) return false;
    written += static_cast<size_t>(result);
  }
  return true;
}
bool CloseFile(FileHandle file) { return _close(file) == 0; }
#else
using FileHandle = int;
constexpr FileHandle kInvalidFile = -1;

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

FileHandle OpenExclusive(const fs::path& path) {
  // 0666 is reduced by the process umask, matching the permissions a normal
  // save produced before writes were made atomic.
  return ::open(path.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666);
}
bool WriteAll(FileHandle file, std::string_view data) {
  size_t written = 0;
  while (written < data.size()) {
    const ssize_t result =
        ::write(file, data.data() + written, data.size() - written);
    if (result <= 0) return false;
    written += static_cast<size_t>(result);
  }
  return true;
}
bool CloseFile(FileHandle file) { return ::close(file) == 0; }
#endif

}  // namespace

bool WriteFileAtomic(const fs::path& path, std::string_view data) {
  // An empty path is what Project::Resolve returns for a path that leaves the
  // project; never turn it into a file in the working directory.
  if (path.empty() || !path.has_filename()) return false;
  std::error_code ec;
  if (path.has_parent_path()) {
    fs::create_directories(path.parent_path(), ec);
  }
  // A predictable "<path>.tmp" let a symlink planted in the project redirect
  // the write. Use a random name and O_EXCL|O_NOFOLLOW so nothing that exists
  // can be opened.
  static std::atomic<uint64_t> counter{0};
  std::random_device random;
  const uint64_t nonce = (static_cast<uint64_t>(random()) << 32) ^ random();
  fs::path temp;
  FileHandle file = kInvalidFile;
  for (int attempt = 0; attempt < 16 && file == kInvalidFile; attempt++) {
    temp = path;
    temp += std::format(".{}.{}.tmp", nonce, counter.fetch_add(1));
    file = OpenExclusive(temp);
  }
  if (file == kInvalidFile) return false;
  const bool written = WriteAll(file, data);
  const bool closed = CloseFile(file);
  if (!written || !closed) {
    fs::remove(temp, ec);
    return false;
  }
  fs::rename(temp, path, ec);
  if (ec) {
    fs::remove(temp, ec);
    return false;
  }
  return true;
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
      const std::string folder =
          it->entry_prefix + key.substr(it->prefix.size());
      for (const mpack::Entry& entry : it->pack->GetEntries()) {
        if (!entry.path.starts_with(folder)) continue;
        std::string rest = entry.path.substr(folder.size());
        if (!rest.empty() && rest.find('/') == std::string::npos)
          add(std::move(rest));
      }
    }
  }
  std::error_code ec;
  for (const auto& entry :
       fs::directory_iterator(dir.empty() ? fs::path(".") : dir, ec)) {
    if (entry.is_regular_file(ec)) add(entry.path().filename().string());
  }
  return names;
}

std::vector<std::string> ListFilesRecursive(const fs::path& dir) {
  std::set<std::string> files;
  std::string key = Normalize(dir);
  if (key == ".") key.clear();
  if (!key.empty() && !key.ends_with('/')) key += '/';
  {
    std::shared_lock lock(g_mounts_mutex);
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
      if (!key.starts_with(it->prefix)) continue;
      const std::string folder =
          it->entry_prefix + key.substr(it->prefix.size());
      for (const mpack::Entry& entry : it->pack->GetEntries()) {
        if (entry.path.starts_with(folder))
          files.insert(entry.path.substr(folder.size()));
      }
    }
  }
  std::error_code ec;
  const fs::path root = dir.empty() ? fs::path(".") : dir;
  for (auto it = fs::recursive_directory_iterator(
           root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->is_regular_file(ec))
      files.insert(it->path().lexically_relative(root).generic_string());
  }
  return {files.begin(), files.end()};
}

fs::path CacheDir() {
  const fs::path dir = [] {
    fs::path base;
    if (const char* custom = std::getenv("MERCURY_CACHE_DIR");
        custom && *custom) {
      return fs::path(custom);
    }
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"))
      base = fs::path(local) / "Mercury";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
      base = fs::path(home) / "Library" / "Caches";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
      base = xdg;
    } else if (const char* home = std::getenv("HOME")) {
      base = fs::path(home) / ".cache";
    }
#endif
    return base.empty() ? base : base / "mercury";
  }();
  if (!dir.empty()) {
    std::error_code ec;
    fs::create_directories(dir, ec);
  }
  return dir;
}

fs::path UserPath(const fs::path& path) { return path; }

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
#else
  const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return fs::current_path(ec);
  return exe.parent_path();
#endif
}

fs::path BundleResourcesDir() {
#if !defined(__APPLE__)
  return {};
#else
  const fs::path exe_dir = ExecutableDir();
  if (exe_dir.filename() != "MacOS" ||
      exe_dir.parent_path().filename() != "Contents") {
    return {};
  }
  const fs::path app = exe_dir.parent_path().parent_path();
  if (app.extension() != ".app") return {};
  const fs::path resources = app / "Contents" / "Resources";
  std::error_code ec;
  return fs::is_directory(resources, ec) ? resources : fs::path();
#endif
}

namespace {

// The first candidate that exists; the last one is returned even when it does
// not, so a development build keeps using its compile-time path.
fs::path FirstExistingDir(const std::vector<fs::path>& candidates) {
  std::error_code ec;
  for (const fs::path& path : candidates) {
    if (!path.empty() && fs::is_directory(path, ec)) return path;
  }
  return candidates.empty() ? fs::path() : candidates.back();
}

// Candidates in preference order: a packaged editor keeps its own copy of the
// engine resources so it works without the checkout, so prefer them to a
// compile-time path that may not exist on the player's machine.
std::vector<fs::path> ResourceCandidates(const char* folder,
                                         const fs::path& compiled) {
  std::vector<fs::path> candidates;
  const fs::path resources = BundleResourcesDir();
  if (!resources.empty()) candidates.push_back(resources / folder);
  const fs::path exe_dir = ExecutableDir();
  if (!exe_dir.empty()) {
    candidates.push_back(exe_dir / folder);
    candidates.push_back(exe_dir / "Resources" / folder);
  }
  candidates.push_back(compiled);
  return candidates;
}

}  // namespace

fs::path EngineUiResourceDir() {
#ifdef MERCURY_UI_RESOURCE_PATH
  const fs::path compiled(MERCURY_UI_RESOURCE_PATH);
#else
  const fs::path compiled("mercury/ui/resources");
#endif
  return FirstExistingDir(ResourceCandidates("ui", compiled));
}

fs::path EngineMaterialDir() {
#ifdef MERCURY_MATERIAL_PATH
  const fs::path compiled(MERCURY_MATERIAL_PATH);
#else
  const fs::path compiled("materials");
#endif
  return FirstExistingDir(ResourceCandidates("materials", compiled));
}

void UseBundledVulkanDriver() {
#if defined(__APPLE__)
  if (std::getenv("VK_DRIVER_FILES") || std::getenv("VK_ICD_FILENAMES")) return;
  const fs::path resources = BundleResourcesDir();
  std::error_code ec;
  // Inside a .app the manifest is in Resources; a game folder written by the
  // editor's Build Game has one next to the executable.
  fs::path icd = resources.empty()
                     ? fs::path()
                     : resources / "vulkan" / "icd.d" / "MoltenVK_icd.json";
  if (icd.empty() || !fs::exists(icd, ec)) {
    icd = ExecutableDir() / "vulkan" / "icd.d" / "MoltenVK_icd.json";
  }
  if (!fs::exists(icd, ec)) return;
  // Both are read by the loader: newer versions prefer VK_DRIVER_FILES,
  // older ones only know VK_ICD_FILENAMES.
  setenv("VK_DRIVER_FILES", icd.c_str(), 1);
  setenv("VK_ICD_FILENAMES", icd.c_str(), 1);
#endif
}

fs::path CopyFileWithoutOverwriting(const fs::path& source,
                                    const fs::path& dest_dir,
                                    std::error_code& ec) {
  ec.clear();
  const fs::path name = source.filename();
  if (name.empty() || name == "." || name == "..") {
    ec = std::make_error_code(std::errc::invalid_argument);
    return {};
  }
  const std::string stem = name.stem().string();
  const std::string extension = name.extension().string();
  constexpr int kMaxAttempts = 1000;
  for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
    const fs::path candidate =
        attempt == 0 ? dest_dir / name
                     : dest_dir / (stem + " (" + std::to_string(attempt) + ")" +
                                   extension);
    // symlink_status looks at the entry itself, so a dangling link counts.
    std::error_code status_ec;
    if (fs::symlink_status(candidate, status_ec).type() !=
        fs::file_type::not_found) {
      continue;
    }
    // Not overwriting: fails if the name appeared in the meantime.
    if (fs::copy_file(source, candidate, fs::copy_options::none, ec)) {
      return candidate;
    }
    if (ec != std::errc::file_exists) return {};
    ec.clear();
  }
  ec = std::make_error_code(std::errc::file_exists);
  return {};
}

}  // namespace Mercury::os
