/*
 * ts_types.h
 *
 * Writes the TypeScript side of the engine into a project, so editors (VS Code, Neovim, Zed...)
 * that run a TypeScript language server get types, hover docs and completion for the "mercury"
 * module. The runtime library is real TypeScript embedded in the engine; the component classes
 * are generated from the TypeRegistry.
 */
#ifndef MERCURY_SCRIPTING_TS_TYPES_H
#define MERCURY_SCRIPTING_TS_TYPES_H

#include <filesystem>
#include <string>

namespace Mercury {

struct EmbeddedFile {
  const char* path;
  const unsigned char* data;
  size_t size;
};
// The files of mercury/scripting/ts/, embedded at build time (see tools/embed_files.py).
extern const EmbeddedFile kTsRuntimeFiles[];
extern const size_t kTsRuntimeFileCount;

/**
 * @brief Writes <out_dir>/mercury/*.ts and <out_dir>/mercury/generated/components.ts
 *
 * Only files whose content changed are rewritten, so language servers don't reload for nothing.
 * @param error set when a file cannot be written
 */
bool WriteTypeScriptTypes(const std::filesystem::path& out_dir, std::string* error = nullptr);

/**
 * @brief Writes <project>/tsconfig.json if the project has none
 */
bool WriteDefaultTsconfig(const std::filesystem::path& project_dir, std::string* error = nullptr);

// The generated components.ts for the components registered right now.
std::string GenerateComponentsTs();

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_TS_TYPES_H
