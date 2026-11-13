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
// Names of the files directly in `dir`, from mounted packs and from disk
// (each once, packs first; folders are not listed).
std::vector<std::string> ListDirectory(const fs::path& dir);
// Paths of every file below `dir`, relative to it and '/'-separated, from
// mounted packs and from disk (each once, sorted).
std::vector<std::string> ListFilesRecursive(const fs::path& dir);
// True if `path` is served from a mounted pack.
bool IsPacked(const fs::path& path);

// The folder holding the running executable.
fs::path ExecutableDir();

// The Contents/Resources folder of the .app bundle holding the running
// executable, or an empty path when the program is not in a bundle.
fs::path BundleResourcesDir();

// Points the Vulkan loader at the MoltenVK ICD shipped inside the .app bundle.
// Does nothing outside a bundle or when the caller already chose driver files.
void UseBundledVulkanDriver();

// A path the user typed on the command line. `bazel run` starts programs inside
// the runfiles tree and puts the folder the user ran it from in
// BUILD_WORKING_DIRECTORY, so a relative path that does not exist from the
// current folder is tried from there. Other paths come back as given.
fs::path UserPath(const fs::path& path);

// A folder for files the engine can rebuild (imported models, settled light
// probes): MERCURY_CACHE_DIR, or the platform's cache folder plus "mercury".
// Created on first use; empty when there is none (the web).
fs::path CacheDir();

}  // namespace Mercury::os

#endif  // MERCURYENGINE_FILESYSTEM_H
