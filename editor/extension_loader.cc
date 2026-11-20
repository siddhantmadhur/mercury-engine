// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "extension_loader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#include <wx/app.h>

#include "cmake_build.h"
#include "console.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "extension.h"
#include "globals.h"
#include "os/dynamic_library.h"
#include "os/process.h"
#include "play_session.h"
#include "project/project.h"

namespace Mercury::EditorUI {

namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

bool IsDynamicLibrary(const std::filesystem::path& path) {
  const std::string extension = ToLower(path.extension().string());
  return extension == ".dylib" || extension == ".so" || extension == ".dll";
}

// True when the project contains native editor-system sources, which is what
// makes the trust prompt necessary.
bool HasSystemSources(const std::filesystem::path& system_dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(system_dir, ec)) return false;
  for (auto it = std::filesystem::recursive_directory_iterator(system_dir, ec);
       !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec)) {
    if (!it->is_regular_file()) continue;
    const std::string extension = ToLower(it->path().extension().string());
    if (extension == ".cc" || extension == ".cpp") return true;
  }
  return false;
}

}  // namespace

struct EditorExtensionLoader::LoadedExtension {
  std::string module_id;
  os::DynamicLibrary library;
  EditorExtension* extension = nullptr;
  void (*destroy)(EditorExtension*) = nullptr;
  std::unique_ptr<EditorContext> context;
  std::filesystem::path load_path;
  bool temporary_copy = false;
};

EditorExtensionLoader::EditorExtensionLoader(EditorScene& scene,
                                             Console& console)
    : scene_(scene), console_(console) {}

EditorExtensionLoader::~EditorExtensionLoader() { UnloadAll(); }

EditorSettings& EditorExtensionLoader::Settings() const {
  return scene_.Settings();
}

std::filesystem::path EditorExtensionLoader::SystemsDir() const {
  auto project = scene_.GetProject();
  if (!project) return {};
  return project->GetRoot() / ".mercury" / "systems";
}

void EditorExtensionLoader::SetFrame(EditorFrame* frame) {
  if (frame == frame_) return;
  if (frame_ && !frame) UnloadAll();
  frame_ = frame;
  // A new frame has no extension UI yet; the next Update reloads for it.
  project_root_.clear();
}

void EditorExtensionLoader::Update() {
  if (!frame_ || unloading_) return;

  auto project = scene_.GetProject();
  const std::filesystem::path root = project ? project->GetRoot() : fs::path();
  if (root != project_root_) {
    UnloadAll();
    project_root_ = root;
    last_selection_revision_ = scene_.SelectionRevision();
    last_play_state_ = static_cast<int>(scene_.PlayRef().GetState());
    if (!root.empty()) Reload();
    return;
  }

  const uint64_t selection = scene_.SelectionRevision();
  if (selection != last_selection_revision_) {
    last_selection_revision_ = selection;
    for (auto& loaded : loaded_) {
      if (loaded->extension) loaded->extension->OnSelectionChanged(scene_);
    }
  }

  const int play_state = static_cast<int>(scene_.PlayRef().GetState());
  if (play_state != last_play_state_) {
    const int running = static_cast<int>(PlaySession::State::kRunning);
    const bool was_running = last_play_state_ == running;
    const bool is_running = play_state == running;
    if (is_running && !was_running) {
      for (auto& loaded : loaded_) {
        if (loaded->extension) loaded->extension->OnPlayStarted();
      }
    } else if (was_running && !is_running) {
      for (auto& loaded : loaded_) {
        if (loaded->extension) loaded->extension->OnPlayStopped();
      }
    }
    last_play_state_ = play_state;
  }
}

bool EditorExtensionLoader::Reload() {
  UnloadAll();
  errors_.clear();
  auto project = scene_.GetProject();
  if (!project || !frame_) return true;

  const fs::path root = project->GetRoot();
  const fs::path system_dir = root / "system";
  std::error_code ec;
  if (!HasSystemSources(system_dir)) {
    // Most projects have no editor systems; that is not an error.
    return true;
  }

  // Native code from a shared project is arbitrary code execution: ask before
  // building anything (build scripts can do work at configure/build time too).
  if (!Settings().HasProjectTrustDecision(root)) {
    if (!trust_prompt_) {
      console_.Info(
          "Editor systems: skipped (the project is not trusted and no trust "
          "prompt is available)");
      return true;
    }
    const std::optional<bool> trusted = trust_prompt_(root);
    if (!trusted) {
      console_.Info(
          "Editor systems: skipped until you trust this project (Project > "
          "Trust Editor Systems)");
      return true;
    }
    Settings().SetProjectTrusted(root, *trusted);
    Settings().Save();
  }
  if (!Settings().IsProjectTrusted(root)) {
    console_.Info("Editor systems: skipped (this project is not trusted)");
    return true;
  }

  std::string error;
  if (!BuildProjectSystems(&error)) {
    console_.Error("Editor systems: " + error);
    errors_.push_back(error);
    return false;
  }

  const fs::path dir = SystemsDir();
  if (!fs::is_directory(dir, ec)) {
    console_.Info("Editor systems: the system/ module built no library");
    return true;
  }

  size_t attempted = 0;
  size_t loaded = 0;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    if (!IsDynamicLibrary(entry.path())) continue;
    attempted++;
    error.clear();
    if (LoadFile(entry.path(), &error)) {
      loaded++;
    } else {
      const std::string message =
          entry.path().filename().string() + ": " + error;
      console_.Error("Editor system " + message);
      errors_.push_back(message);
    }
  }

  if (loaded > 0) frame_->ApplyExtensions();
  console_.Info("Editor systems: loaded " + std::to_string(loaded) + " of " +
                std::to_string(attempted));
  return true;
}

bool EditorExtensionLoader::BuildProjectSystems(std::string* error) {
  auto project = scene_.GetProject();
  if (!project) {
    *error = "no project is open";
    return false;
  }
  const std::string target = project->GetBuildSystemsTarget();
  if (target.empty()) {
    *error =
        "the project has a system/ folder but no [build] target, so the"
        " editor cannot build it";
    return false;
  }
  const fs::path build_dir = cmake_build::BuildDir();
  std::error_code ec;
  if (build_dir.empty() || !fs::exists(build_dir / "CMakeCache.txt", ec)) {
    *error =
        "the editor has no configured CMake build directory; run"
        " `cmake --preset release` in the engine checkout first";
    return false;
  }

  const std::string target_name = target;
  const std::vector<std::string> args =
      cmake_build::BuildArguments(target_name);
  os::Process process;
  std::string start_error;
  if (!process.Start(args, build_dir, &start_error)) {
    *error = start_error;
    return false;
  }

  console_.Info("Building editor systems (target " + target_name + ")...");
  while (process.IsRunning()) {
    for (const std::string& line : process.TakeOutput()) {
      console_.AddProcessOutput(Console::Source::kBuild, line);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  for (const std::string& line : process.TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, line);
  }
  const std::optional<int> code = process.GetExitCode();
  if (!code || *code != 0) {
    *error = "building target " + target_name + " failed";
    return false;
  }
  return true;
}

bool EditorExtensionLoader::LoadFile(const std::filesystem::path& file,
                                     std::string* error) {
  const int serial = ++load_serial_;
  const std::string module_id =
      file.stem().string() + "#" + std::to_string(serial);

  fs::path load_path = file;
  bool temporary_copy = false;
#if defined(__APPLE__)
  // dlopen returns the already-loaded image for the same path, so a reload
  // would keep running the old code. Copy under a new name first.
  const fs::path loaded_dir = file.parent_path() / ".loaded";
  std::error_code copy_ec;
  fs::create_directories(loaded_dir, copy_ec);
  if (!copy_ec) {
    const fs::path copy =
        loaded_dir / (file.stem().string() + "-" + std::to_string(serial) +
                      file.extension().string());
    fs::copy_file(file, copy, fs::copy_options::overwrite_existing, copy_ec);
    if (!copy_ec) {
      load_path = copy;
      temporary_copy = true;
    }
  }
  if (!temporary_copy) {
    if (error) {
      *error = "could not make a reloadable copy of " + file.string();
    }
    return false;
  }
#endif

  // Remove the macOS copy again if loading fails partway through.
  struct TempFileGuard {
    std::filesystem::path path;
    bool active = false;
    ~TempFileGuard() {
      if (active) {
        std::error_code ec;
        fs::remove(path, ec);
      }
    }
  } temp_guard{load_path, temporary_copy};

  auto loaded = std::make_unique<LoadedExtension>();
  loaded->module_id = module_id;
  loaded->load_path = load_path;
  loaded->temporary_copy = temporary_copy;

  std::string load_error;
  if (!loaded->library.Load(load_path, &load_error)) {
    if (error) *error = load_error;
    if (temporary_copy) {
      std::error_code ec;
      fs::remove(load_path, ec);
    }
    return false;
  }

  using EntryFn = const EditorExtensionEntry* (*)();
  void* symbol =
      loaded->library.GetSymbol("mercury_editor_extension_v1", &load_error);
  if (!symbol) {
    if (error) *error = load_error;
    return false;
  }
  const EditorExtensionEntry* entry = reinterpret_cast<EntryFn>(symbol)();
  if (!entry) {
    if (error) *error = "mercury_editor_extension_v1 returned null";
    return false;
  }
  if (entry->abi_version != kEditorExtensionAbiVersion) {
    if (error) {
      *error = "extension ABI " + std::to_string(entry->abi_version) +
               ", the editor expects " +
               std::to_string(kEditorExtensionAbiVersion);
    }
    return false;
  }
  if (!entry->engine_version ||
      !IsExtensionVersionCompatible(entry->engine_version,
                                    MERCURY_ENGINE_VERSION)) {
    if (error) {
      *error = "extension built for engine " +
               std::string(entry->engine_version ? entry->engine_version
                                                 : "(none)") +
               ", this editor is " MERCURY_ENGINE_VERSION;
    }
    return false;
  }
  if (!entry->create || !entry->destroy) {
    if (error) *error = "the extension entry point has no create/destroy";
    return false;
  }

  loaded->destroy = entry->destroy;
  loaded->extension = entry->create();
  if (!loaded->extension) {
    if (error) *error = "the extension factory returned null";
    return false;
  }
  loaded->context = std::unique_ptr<EditorContext>(
      new EditorContext(module_id, frame_, &scene_, &Settings(), &console_,
                        frame_ ? &frame_->GetAuiManager() : nullptr));
  loaded->extension->OnLoad(*loaded->context);
  loaded->extension->OnProjectOpened(scene_);

  console_.Info("Editor system loaded: " + file.filename().string());
  temp_guard.active = false;
  loaded_.push_back(std::move(loaded));
  return true;
}

void EditorExtensionLoader::SetTrustPrompt(TrustPrompt prompt) {
  trust_prompt_ = std::move(prompt);
}

bool EditorExtensionLoader::IsProjectTrusted() const {
  auto project = scene_.GetProject();
  return project && Settings().IsProjectTrusted(project->GetRoot());
}

void EditorExtensionLoader::SetProjectTrusted(bool trusted) {
  auto project = scene_.GetProject();
  if (!project) return;
  Settings().SetProjectTrusted(project->GetRoot(), trusted);
  Settings().Save();
}

void EditorExtensionLoader::UnloadAll() {
  if (unloading_) return;
  unloading_ = true;
  auto process_wx_deletes = [] {
    if (!wxTheApp) return;
    // ProcessPendingEvents drains wxPendingDelete (wx 3.3 does it there);
    // ProcessIdle also flushes any remaining deferred window deletions.
    wxTheApp->ProcessPendingEvents();
    wxTheApp->ProcessIdle();
  };

  for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
    LoadedExtension& loaded = **it;
    if (loaded.extension) {
      loaded.extension->OnProjectClosed();
      loaded.extension->OnUnload();
    }

    // Top-level windows the module created itself. Destroy() defers the
    // deletion, so process the queue while its code is still mapped.
    if (loaded.context) {
      for (wxWindow* window : loaded.context->GetTrackedWindows()) {
        if (window) window->Destroy();
      }
    }
    process_wx_deletes();

    // The registry entries are std::functions that live in the module. Remove
    // them, then rebuild the Inspector while the library is still mapped so a
    // custom inspector window is destroyed with valid vtables.
    EditorExtensionRegistry::Instance().RemoveModule(loaded.module_id);
    if (frame_) frame_->RemoveExtensionUI(loaded.module_id);
    process_wx_deletes();

    // The context also unregisters the module's asset importers (more
    // std::functions in the module).
    loaded.context.reset();
    process_wx_deletes();

    if (loaded.extension && loaded.destroy) loaded.destroy(loaded.extension);
    loaded.extension = nullptr;
    loaded.library.Unload();
    if (loaded.temporary_copy) {
      std::error_code ec;
      fs::remove(loaded.load_path, ec);
    }
  }
  loaded_.clear();
  unloading_ = false;
  if (frame_) {
    frame_->ApplyExtensions();
  }
}

}  // namespace Mercury::EditorUI
