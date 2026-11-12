/*
 * script_builder.h
 *
 * Compiles a project's AngelScript sources. There is no external bundler:
 * ScriptRuntime compiles the scripts folder with CScriptBuilder in-process, so
 * Build is a validation step and writes a timestamp for the player.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_BUILDER_H
#define MERCURY_SCRIPTING_AS_SCRIPT_BUILDER_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Mercury {

struct BuildOptions {
  // Kept for callers that build a release: AngelScript sources are small, and
  // the engine has no minifier to run.
  bool release = false;
  // Folder (relative to the project) the editor uses for generated files.
  std::filesystem::path output_dir = ".mercury/scripts";
  // Called with each line of compiler output as it arrives.
  std::function<void(const std::string&)> on_line;
};

class ScriptBuilder {
 public:
  /**
   * @brief Compiles the project's .as files and registers their classes
   * @return false if the project does not compile
   */
  static bool Build(const std::filesystem::path& project_dir, const BuildOptions& options = {},
                    std::string* log = nullptr);

  // True when every .as is older than the stamp Build writes.
  static bool IsUpToDate(const std::filesystem::path& project_dir);

  // Every .as under the project's scripts folder, sorted.
  static std::vector<std::filesystem::path> ScriptFiles(const std::filesystem::path& project_dir);

  static std::filesystem::path StampPath(const std::filesystem::path& project_dir);
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_BUILDER_H
