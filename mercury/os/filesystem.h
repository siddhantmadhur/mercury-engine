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

/**
 * @brief Copies `source` into `dest_dir` without touching anything already
 * there
 *
 * The copy keeps the file's name; if something with that name exists (a file, a
 * folder, or a symlink, even a dangling one) it is left alone and the copy is
 * named "name (1).ext", "name (2).ext" and so on. So a drop can neither replace
 * a project asset nor write through a symlink to somewhere else.
 *
 * @return The path written, or an empty path with `ec` set on failure
 */
fs::path CopyFileWithoutOverwriting(const fs::path& source,
                                    const fs::path& dest_dir,
                                    std::error_code& ec);
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

/**
 * @brief Where the Mercury SDK (headers, archives, CMake package) lives
 *
 * A packaged editor ships it in Contents/Resources/sdk. A development build
 * falls back to the checkout: `mercury/` and `third_party/` for headers,
 * `cmake/` for the package files, and the build tree's `lib/` and `generated/`.
 */
struct EngineSdkPaths {
  std::filesystem::path root;
  std::vector<std::filesystem::path> include_dirs;
  std::filesystem::path cmake_dir;
  std::filesystem::path lib_dir;
  // The engine's build directory (development fallback only).
  std::filesystem::path build_dir;
  bool development = false;

  bool IsValid() const { return !root.empty(); }
};

// Resolves the SDK the running program belongs to.
EngineSdkPaths EngineSdkDir();

// The same resolution with explicit locations, for tests. `engine_dir` and
// `build_dir` are the development fallback (either may be empty).
EngineSdkPaths MakeEngineSdkPaths(const std::filesystem::path& exe_dir,
                                  const std::filesystem::path& bundle_resources,
                                  const std::filesystem::path& engine_dir,
                                  const std::filesystem::path& build_dir);

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

/**
 * @brief Ensures <project>/.mercury is a real directory, never a symlink
 *
 * Every generated project file lives below this folder. If the entry exists
 * as a symlink (or any non-directory), it is refused, so a cloned project
 * cannot redirect editor writes outside itself. Creates the folder when
 * missing. Returns an empty path with `error` filled on failure.
 */
fs::path EnsureProjectCacheDir(const fs::path& project_root,
                               std::string* error = nullptr);

// A direct child of the project cache ("scripts", "systems"): created when
// missing, refused when it is a symlink or not a directory.
fs::path EnsureProjectCacheSubdir(const fs::path& cache_dir,
                                  std::string_view name,
                                  std::string* error = nullptr);

/**
 * @brief The project's own CMake build folder, marked as editor-created
 *
 * An existing `<cache>/build/<config>` without the `.mercury-build` marker is
 * shipped build output (CMakeCache + build.ninja): it is refused rather than
 * executed, because a configure/build would run the shipped rules. The folder
 * is created with the marker when missing.
 */
fs::path EnsureProjectBuildDir(const fs::path& project_root,
                               std::string_view config,
                               std::string* error = nullptr);

// True when `dir` carries the editor's marker (see EnsureProjectBuildDir).
bool IsEditorProjectBuildDir(const fs::path& dir);

}  // namespace Mercury::os

#endif  // MERCURYENGINE_FILESYSTEM_H
