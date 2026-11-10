/*
 * script_builder.h
 *
 * Turns a project's TypeScript into the bundle the player runs. esbuild bundles
 * the project's scripts folder and the engine's runtime library into `.mercury/scripts/game.js`; if `tsc`
 * is installed it also type-checks. (esbuild does not check types; the editor's language server
 * does, while you type.)
 */
#ifndef MERCURY_SCRIPTING_JS_SCRIPT_BUILDER_H
#define MERCURY_SCRIPTING_JS_SCRIPT_BUILDER_H

#include <filesystem>
#include <functional>
#include <string>

namespace Mercury {

struct BuildOptions {
  // Minify the bundle (shipped games).
  bool release = false;
  // Folder (relative to the project) that gets entry.ts, game.js and game.js.map. It must be two
  // levels deep in the project, because entry.ts imports "../../scripts/...".
  std::filesystem::path output_dir = ".mercury/scripts";
  // Called with each line of esbuild's and tsc's output as it arrives.
  std::function<void(const std::string& line)> on_line;
};

class ScriptBuilder {
 public:
  /**
   * @brief Writes the types, generates the entry file, runs esbuild (and tsc when available)
   * @param log gets every output line, one per line (may be null)
   * @return false if esbuild or tsc failed
   */
  static bool Build(const std::filesystem::path& project_dir, const BuildOptions& options = {},
                    std::string* log = nullptr);

  // True when game.js is newer than every script (.ts under scripts/) and tsconfig.json.
  static bool IsUpToDate(const std::filesystem::path& project_dir);

  static std::filesystem::path BundlePath(const std::filesystem::path& project_dir) {
    return project_dir / ".mercury" / "scripts" / "game.js";
  }
  static std::filesystem::path SourceMapPath(const std::filesystem::path& project_dir) {
    return project_dir / ".mercury" / "scripts" / "game.js.map";
  }
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_JS_SCRIPT_BUILDER_H
