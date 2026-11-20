// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * extension_loader.h
 *
 * Builds and loads a project's `system/` modules into the editor. The modules
 * are ordinary dynamic libraries; see editor/extension.h for the API. The
 * loader owns their lifetime: it unloads them when the project closes, when
 * the editor frame goes away, and when the user picks Project > Reload
 * Systems.
 */
#ifndef MERCURY_EDITOR_EXTENSION_LOADER_H
#define MERCURY_EDITOR_EXTENSION_LOADER_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Mercury {

class Console;
class EditorSettings;
class EditorScene;

}  // namespace Mercury

namespace Mercury::EditorUI {

class EditorContext;
class EditorExtension;
class EditorFrame;

class EditorExtensionLoader {
 public:
  EditorExtensionLoader(EditorScene& scene, Console& console);
  ~EditorExtensionLoader();
  EditorExtensionLoader(const EditorExtensionLoader&) = delete;
  EditorExtensionLoader& operator=(const EditorExtensionLoader&) = delete;
  // The editor frame extension UI is created in. Setting a different frame
  // unloads the current modules; the next Update loads them for the new one.
  void SetFrame(EditorFrame* frame);

  // Polls project/selection/play transitions. Call from the editor's UI tick.
  void Update();

  // Asked the first time a project with system/ sources is opened. Returning
  // nullopt means "decide later": no decision is stored and nothing is built.
  using TrustPrompt =
      std::function<std::optional<bool>(const std::filesystem::path& root)>;
  void SetTrustPrompt(TrustPrompt prompt);
  // The stored decision for the open project (false when unknown).
  bool IsProjectTrusted() const;
  // Stores (and saves) a decision for the open project.
  void SetProjectTrusted(bool trusted);

  /**
   * @brief Unloads everything, builds <project>/system and loads its modules
   * @return false when the build failed or the project has no build target;
   * errors are also logged to the console.
   */
  bool Reload();
  void UnloadAll();

  // Loads one dynamic library without building. Used by Reload and by tests.
  bool LoadFile(const std::filesystem::path& file,
                std::string* error = nullptr);

  size_t GetLoadedCount() const { return loaded_.size(); }
  const std::vector<std::string>& GetErrors() const { return errors_; }

 private:
  struct LoadedExtension;

  void UnloadAllNow();
  bool BuildProjectSystems(std::string* error);
  std::filesystem::path SystemsDir() const;
  // The per-project cache folder for reload copies (0700, never a symlink).
  std::filesystem::path TempDirForProject() const;
  void CleanStaleTempCopies() const;
  EditorSettings& Settings() const;

  EditorFrame* frame_ = nullptr;
  EditorScene& scene_;
  Console& console_;
  std::vector<std::unique_ptr<LoadedExtension>> loaded_;
  std::vector<std::string> errors_;
  std::filesystem::path project_root_;
  TrustPrompt trust_prompt_;
  uint64_t last_selection_revision_ = 0;
  int last_play_state_ = -1;
  int load_serial_ = 0;
  // True while UnloadAll is processing wx events; Update must not re-enter.
  bool unloading_ = false;
};

}  // namespace Mercury::EditorUI

namespace Mercury::EditorUI {

/**
 * @brief Finds the module a project's systems target built
 *
 * Only `<target>.so`, `<target>.dylib` or `<target>.dll` is accepted, and only
 * as a regular file: a symlink is refused, and libraries left over from other
 * builds are never loaded.
 */
std::filesystem::path FindSystemModule(const std::filesystem::path& dir,
                                       const std::string& target,
                                       std::string* error = nullptr);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_EXTENSION_LOADER_H
