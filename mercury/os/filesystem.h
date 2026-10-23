/*
 * filesystem.h
 *
 * Reads asset files, either from disk or from a mounted .mpack (see
 * pack/mpack.h). A pack is mounted at a path prefix: a read of any path under
 * that prefix is served from the pack, and everything else comes from disk.
 * The prefix doesn't have to exist, so a shipped game mounts game.mpack at its
 * project root and engine.mpack at the engine's resource paths, and the paths
 * everything already resolves stay the same.
 *
 * With nothing mounted (the editor, Play, tests) every read goes to disk.
 */
#ifndef MERCURYENGINE_FILESYSTEM_H
#define MERCURYENGINE_FILESYSTEM_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace fs = std::filesystem;

namespace Mercury::mpack {
class Reader;
}

namespace Mercury::os {

/**
 * @brief Serves reads under `prefix` from `pack`
 * @param entry_prefix prepended to the path below `prefix` to name the entry,
 * e.g. "ui/" so <prefix>/fonts/a.ttf reads the entry ui/fonts/a.ttf
 *
 * Later mounts are searched first. A path under a mount that the pack
 * doesn't have is read from disk.
 */
void Mount(const fs::path& prefix, std::shared_ptr<const mpack::Reader> pack,
           std::string entry_prefix = "");
void UnmountAll();
bool HasMounts();

std::optional<std::vector<uint8_t>> ReadBytes(const fs::path& path);
std::optional<std::string> ReadText(const fs::path& path);
bool Exists(const fs::path& path);
// True if `path` is served from a mounted pack.
bool IsPacked(const fs::path& path);

// The folder holding the running executable.
fs::path ExecutableDir();

}  // namespace Mercury::os

#endif  // MERCURYENGINE_FILESYSTEM_H
