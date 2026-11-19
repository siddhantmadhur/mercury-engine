// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
#include <string_view>
#include <vector>
namespace fs = std::filesystem;

namespace Mercury::mpack {
class Reader;
}

namespace Mercury::os {

/**
 * @brief Serves reads under `prefix` from `pack`
 * @param entry_prefix prepended to the path below `prefix` to name the entry,
 * e.g. "ui/" so `<prefix>/fonts/a.ttf` reads the entry `ui/fonts/a.ttf`
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
/**
 * @brief Writes `data` to `path`, replacing it atomically
 *
 * The data goes to a randomly named file beside `path`, opened so that an
 * existing file (in particular a symlink) is never followed, and is then
 * renamed over `path`.
 */
bool WriteFileAtomic(const fs::path& path, std::string_view data);
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

// Where the engine's loose UI theme, fonts and icons live: the checkout's
// mercury/ui/resources in a development build, or Contents/Resources/ui in a
// packaged editor. A shipped game mounts engine.mpack at this path instead.
fs::path EngineUiResourceDir();

// Where the compiled Filament materials (.filamat) live:
// <build>/materials in a development build, or Contents/Resources/materials in
// a packaged editor. A shipped game mounts engine.mpack at this path instead.
fs::path EngineMaterialDir();

// Points the Vulkan loader at the MoltenVK ICD shipped next to the program (in
// a .app bundle's Resources, or in the game folder that Build Game wrote).
// Does nothing when the caller already chose driver files.
void UseBundledVulkanDriver();

// A path the user typed on the command line. Relative paths are resolved by
// the program's working directory, which is the folder the user ran it from;
// paths are otherwise returned unchanged so error messages can name exactly
// what was typed.
fs::path UserPath(const fs::path& path);

// A folder for files the engine can rebuild (imported models, settled light
// probes): MERCURY_CACHE_DIR, or the platform's cache folder plus "mercury".
// Created on first use; empty when there is none (the web).
fs::path CacheDir();

}  // namespace Mercury::os

#endif  // MERCURYENGINE_FILESYSTEM_H
