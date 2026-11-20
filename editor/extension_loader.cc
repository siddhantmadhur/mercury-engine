// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "extension_loader.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

#include <wx/app.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include "cmake_build.h"
#include "console.h"
#include "editor_frame.h"
#include "editor_scene.h"
#include "extension.h"
#include "globals.h"
#include "os/dynamic_library.h"
#include "os/filesystem.h"
#include "os/process.h"
#include "play_session.h"
#include "project/project.h"
#include "project_trust.h"
#include "project_scaffold.h"

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

// A stable folder name for a project's reload copies.
std::string ProjectKey(const std::filesystem::path& root) {
  std::error_code ec;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(root, ec);
  const std::string text = (ec ? root : canonical).lexically_normal().string();
  uint64_t hash = 1469598103934665603ull;  // FNV-1a
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return std::format("{:016x}", hash);
}

// Creates `dest` exclusively (O_EXCL on POSIX) and copies `source` into it.
bool CopyExclusive(const std::filesystem::path& source,
                   const std::filesystem::path& dest, std::string* error) {
  std::ifstream in(source, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot read " + source.string();
    return false;
  }
  const std::string bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
#if defined(_WIN32)
  std::error_code ec;
  std::filesystem::copy_file(source, dest, std::filesystem::copy_options::none,
                             ec);
  if (ec) {
    if (error) *error = dest.string() + ": " + ec.message();
    return false;
  }
  std::filesystem::permissions(
      dest,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace, ec);
#else
  const int fd = ::open(dest.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
  if (fd < 0) {
    if (error) *error = dest.string() + ": " + std::strerror(errno);
    return false;
  }
  size_t written = 0;
  while (written < bytes.size()) {
    const ssize_t count =
        ::write(fd, bytes.data() + written, bytes.size() - written);
    if (count < 0) {
      if (errno == EINTR) continue;
      if (error) *error = dest.string() + ": " + std::strerror(errno);
      ::close(fd);
      ::unlink(dest.c_str());
      return false;
    }
    written += static_cast<size_t>(count);
  }
  ::close(fd);
#endif
  return true;
}

}  // namespace

std::filesystem::path FindSystemModule(const std::filesystem::path& dir,
                                       const std::string& target,
                                       std::string* error) {
  if (target.empty()) {
    if (error) *error = "the project has no systems target";
    return {};
  }
  // A target is used as a file name below `dir`; reject path separators and
  // anything CMake would not accept, so it cannot escape the folder.
  if (!Project::IsValidTargetName(target)) {
    if (error) *error = "invalid systems target \"" + target + "\"";
    return {};
  }
  for (const char* suffix : {".so", ".dylib", ".dll"}) {
    const std::filesystem::path candidate = dir / (target + suffix);
    std::error_code ec;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(candidate, ec);
    if (ec || !std::filesystem::exists(status)) continue;
    if (std::filesystem::is_symlink(status)) {
      if (error) *error = "refusing symlinked module " + candidate.string();
      continue;
    }
    if (std::filesystem::is_regular_file(status)) return candidate;
  }
  if (error) {
    *error = "no built module " + target + " in " + dir.string();
  }
  return {};
}

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
    : scene_(scene), console_(console) {
  EditorExtensionRegistry::Instance().SetErrorSink(
      [this](std::string_view module, std::string_view message) {
        console_.Error(std::string(module) + ": " + std::string(message));
      });
}

EditorExtensionLoader::~EditorExtensionLoader() {
  *alive_ = false;
  UnloadAllNow();
  EditorExtensionRegistry::Instance().SetErrorSink({});
}

EditorSettings& EditorExtensionLoader::Settings() const {
  return scene_.Settings();
}

std::filesystem::path EditorExtensionLoader::SystemsDir() const {
  auto project = scene_.GetProject();
  if (!project) return {};
  return project->GetRoot() / ".mercury" / "systems";
}

std::filesystem::path EditorExtensionLoader::TempDirForProject() const {
  auto project = scene_.GetProject();
  const fs::path cache = os::CacheDir();
  if (cache.empty()) return {};
  // Tests and hosts without a project still get an isolated folder.
  const std::string key =
      project ? ProjectKey(project->GetRoot()) : std::string("no-project");
  // os::CacheDir() already ends in "mercury".
  const fs::path dir = cache / "systems" / key;
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return {};
  // Never follow a symlink planted in the cache location.
  const fs::file_status status = fs::symlink_status(dir, ec);
  if (ec || fs::is_symlink(status) || !fs::is_directory(status)) return {};
  fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
  return dir;
}

void EditorExtensionLoader::CleanStaleTempCopies() const {
  const fs::path dir = TempDirForProject();
  if (dir.empty()) return;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::file_status status = it->symlink_status(ec);
    if (ec) break;
    if (fs::is_regular_file(status)) fs::remove(it->path(), ec);
  }
}

void EditorExtensionLoader::SetFrame(EditorFrame* frame) {
  if (frame == frame_) return;
  if (frame_ && !frame) UnloadAll();
  frame_ = frame;
  // A new frame has no extension UI yet; the next Update reloads for it.
  project_root_.clear();
}

void EditorExtensionLoader::Update() {
  if (!frame_ || unloading_ ||
      EditorExtensionRegistry::Instance().InCallback()) {
    return;
  }

  PollSystemsBuild();

  auto project = scene_.GetProject();
  const std::filesystem::path root = project ? project->GetRoot() : fs::path();
  if (root != project_root_) {
    CancelSystemsBuild();
    UnloadAllNow();
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
      if (!loaded->extension) continue;
      RunExtensionCallback(loaded->module_id, "selection changed", [&] {
        loaded->extension->OnSelectionChanged(scene_);
      });
    }
  }

  const int play_state = static_cast<int>(scene_.PlayRef().GetState());
  if (play_state != last_play_state_) {
    const int running = static_cast<int>(PlaySession::State::kRunning);
    const bool was_running = last_play_state_ == running;
    const bool is_running = play_state == running;
    if (is_running && !was_running) {
      for (auto& loaded : loaded_) {
        if (!loaded->extension) continue;
        RunExtensionCallback(loaded->module_id, "play started",
                             [&] { loaded->extension->OnPlayStarted(); });
      }
    } else if (was_running && !is_running) {
      for (auto& loaded : loaded_) {
        if (!loaded->extension) continue;
        RunExtensionCallback(loaded->module_id, "play stopped",
                             [&] { loaded->extension->OnPlayStopped(); });
      }
    }
    last_play_state_ = play_state;
  }
}

bool EditorExtensionLoader::Reload() {
  if (EditorExtensionRegistry::Instance().InCallback()) {
    // A module callback (or a modal dialog's nested loop) asked for a reload.
    // Defer it so the module whose code is running is not unloaded under it.
    if (wxTheApp) {
      std::weak_ptr<bool> alive = alive_;
      wxTheApp->CallAfter([this, alive]() {
        auto token = alive.lock();
        if (token && *token) Reload();
      });
    }
    return true;
  }

  UnloadAllNow();
  errors_.clear();
  CleanStaleTempCopies();
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
    SetProjectTrust(Settings(), root, *trusted, console_);
  }
  if (!Settings().IsProjectTrusted(root)) {
    console_.Info("Editor systems: skipped (this project is not trusted)");
    return true;
  }

  return StartSystemsBuild();
}

bool EditorExtensionLoader::StartSystemsBuild() {
  auto project = scene_.GetProject();
  if (!project || !frame_) return true;
  // Defense in depth: Reload prompts and checks, but the build itself must
  // never run project CMake code for an untrusted workspace.
  if (!Settings().IsProjectTrusted(project->GetRoot())) {
    const std::string error =
        "the project is not trusted, so its editor systems were not built";
    console_.Error("Editor systems: " + error);
    errors_.push_back(error);
    return false;
  }
  const std::string target = project->GetBuildSystemsTarget();
  if (target.empty()) {
    const std::string error =
        "the project has a system/ folder but no [build] target, so the "
        "editor cannot build it";
    console_.Error("Editor systems: " + error);
    errors_.push_back(error);
    return false;
  }
  const fs::path sdk_cmake = cmake_build::SdkCmakeDir();
  if (sdk_cmake.empty()) {
    const std::string error =
        "the editor's SDK has no CMake package. A development checkout must "
        "be configured once; a packaged editor should include Contents/"
        "Resources/sdk.";
    console_.Error("Editor systems: " + error);
    errors_.push_back(error);
    return false;
  }

  CancelSystemsBuild();
  build_project_root_ = project->GetRoot();
  build_target_ = target;
  build_dir_ = cmake_build::ProjectBuildDir(build_project_root_);
  if (!cmake_build::TryAcquireProjectBuild(build_project_root_)) {
    console_.Warn(
        "Editor systems: another project build is running; the systems module "
        "will be built on the next reload");
    return true;
  }
  holds_build_lock_ = true;
  std::string error;
  if (cmake_build::NeedsConfigure(build_project_root_, build_dir_)) {
    console_.Info("Configuring the project build in " + build_dir_.string());
    const std::vector<std::string> args = cmake_build::ConfigureArguments(
        build_project_root_, build_dir_, sdk_cmake);
    if (!LaunchSystemsProcess(args, true, &error)) {
      ReleaseProjectBuild();
      console_.Error("Editor systems: " + error);
      errors_.push_back(error);
      return false;
    }
  } else {
    const std::vector<std::string> args =
        cmake_build::BuildArguments(build_dir_, target);
    if (!LaunchSystemsProcess(args, false, &error)) {
      ReleaseProjectBuild();
      console_.Error("Editor systems: " + error);
      errors_.push_back(error);
      return false;
    }
  }
  return true;
}

bool EditorExtensionLoader::LaunchSystemsProcess(
    const std::vector<std::string>& args, bool configure, std::string* error) {
  std::string command;
  for (const std::string& arg : args) {
    command += (command.empty() ? "" : " ") + arg;
  }
  console_.Add(Console::Level::kInfo, Console::Source::kBuild, "> " + command);

  auto process = std::make_unique<os::Process>();
  std::string start_error;
  if (!process->Start(args, build_project_root_, &start_error)) {
    if (error) *error = start_error;
    return false;
  }
  build_process_ = std::move(process);
  build_configuring_ = configure;
  if (!configure) {
    console_.Info("Building editor systems (target " + build_target_ + ")...");
  }
  return true;
}

void EditorExtensionLoader::PollSystemsBuild() {
  if (!build_process_) return;
  for (std::string& line : build_process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  if (build_process_->IsRunning()) return;
  for (std::string& line : build_process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  const int code = build_process_->GetExitCode().value_or(-1);
  build_process_.reset();

  if (build_configuring_) {
    build_configuring_ = false;
    if (code != 0) {
      ReleaseProjectBuild();
      const std::string error = "configuring the project build failed";
      console_.Error("Editor systems: " + error + " (see the build output)");
      errors_.push_back(error);
      return;
    }
    RefreshProjectCompileCommands(build_project_root_);
    std::string error;
    const std::vector<std::string> args =
        cmake_build::BuildArguments(build_dir_, build_target_);
    if (!LaunchSystemsProcess(args, false, &error)) {
      ReleaseProjectBuild();
      console_.Error("Editor systems: " + error);
      errors_.push_back(error);
    }
    return;
  }

  ReleaseProjectBuild();
  if (code != 0) {
    const std::string error = "building target " + build_target_ + " failed";
    console_.Error("Editor systems: " + error + " (see the build output)");
    errors_.push_back(error);
    return;
  }
  LoadBuiltSystems();
}

void EditorExtensionLoader::CancelSystemsBuild() {
  if (build_process_) {
    build_process_->Kill();
    for (std::string& line : build_process_->TakeOutput()) {
      console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
    }
    build_process_.reset();
  }
  build_configuring_ = false;
  ReleaseProjectBuild();
}

void EditorExtensionLoader::ReleaseProjectBuild() {
  if (!holds_build_lock_) return;
  cmake_build::ReleaseProjectBuild(build_project_root_);
  holds_build_lock_ = false;
}

void EditorExtensionLoader::LoadBuiltSystems() {
  auto project = scene_.GetProject();
  if (!project || !frame_) return;
  // The project may have changed while the build ran.
  if (project->GetRoot() != build_project_root_ ||
      project->GetBuildSystemsTarget() != build_target_) {
    return;
  }

  std::error_code ec;
  const fs::path dir = SystemsDir();
  if (!fs::is_directory(dir, ec)) {
    console_.Info("Editor systems: the system/ module built no library");
    return;
  }

  // Load only the output of the project's systems target. Anything else in the
  // folder (a prebuilt library, an old build, a symlink) is ignored.
  std::string error;
  const fs::path module = FindSystemModule(dir, build_target_, &error);
  for (fs::directory_iterator it(dir, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (!IsDynamicLibrary(it->path())) continue;
    if (!module.empty() && it->path() == module) continue;
    console_.Warn("Editor systems: ignoring " + it->path().filename().string() +
                  " (not the " + build_target_ + " output)");
  }
  if (module.empty()) {
    console_.Warn("Editor systems: " + error);
    return;
  }

  if (!LoadFile(module, &error)) {
    const std::string message = module.filename().string() + ": " + error;
    console_.Error("Editor system " + message);
    errors_.push_back(message);
    return;
  }
  if (frame_) frame_->ApplyExtensions();
}

bool EditorExtensionLoader::LoadFile(const std::filesystem::path& file,
                                     std::string* error) {
  std::error_code status_ec;
  const fs::file_status file_status = fs::symlink_status(file, status_ec);
  if (status_ec || fs::is_symlink(file_status) ||
      !fs::is_regular_file(file_status)) {
    if (error) *error = "not a regular file: " + file.string();
    return false;
  }

  const int serial = ++load_serial_;
  const std::string module_id =
      file.stem().string() + "#" + std::to_string(serial);

  fs::path load_path = file;
  bool temporary_copy = false;
#if defined(__APPLE__)
  // dlopen returns the already-loaded image for the same path, so a reload
  // would keep running the old code. Copy to the user cache under a new,
  // exclusively created name first.
  const fs::path dir = TempDirForProject();
  if (dir.empty()) {
    if (error) *error = "no writable cache directory for module copies";
    return false;
  }
  for (int attempt = 0; attempt < 50 && !temporary_copy; attempt++) {
    const fs::path copy =
        dir / (file.stem().string() + "-" + std::to_string(serial) + "-" +
               std::to_string(attempt) + file.extension().string());
    std::string copy_error;
    if (CopyExclusive(file, copy, &copy_error)) {
      load_path = copy;
      temporary_copy = true;
    } else if (!fs::exists(copy)) {
      if (error) *error = copy_error;
      return false;
    }
  }
  if (!temporary_copy) {
    if (error) *error = "could not create a reloadable module copy";
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

  // OnLoad is the one call whose failure aborts the load.
  try {
    loaded->extension->OnLoad(*loaded->context);
  } catch (const std::exception& e) {
    if (error) *error = "OnLoad: " + std::string(e.what());
    return false;
  } catch (...) {
    if (error) *error = "OnLoad: unknown exception";
    return false;
  }
  RunExtensionCallback(module_id, "project opened",
                       [&] { loaded->extension->OnProjectOpened(scene_); });

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
  SetProjectTrust(Settings(), project->GetRoot(), trusted, console_);
}

void EditorExtensionLoader::UnloadAll() {
  if (EditorExtensionRegistry::Instance().InCallback()) {
    // Do not unload the module whose code is running (or whose modal dialog
    // is executing). The deferred call runs from the event loop.
    if (wxTheApp) {
      std::weak_ptr<bool> alive = alive_;
      wxTheApp->CallAfter([this, alive]() {
        auto token = alive.lock();
        if (token && *token) UnloadAll();
      });
    }
    return;
  }
  UnloadAllNow();
}

void EditorExtensionLoader::UnloadAllNow() {
  CancelSystemsBuild();
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
      RunExtensionCallback(loaded.module_id, "project closed",
                           [&] { loaded.extension->OnProjectClosed(); });
      RunExtensionCallback(loaded.module_id, "unload",
                           [&] { loaded.extension->OnUnload(); });
    }

    // Remove module event handlers before any wx object they are attached to
    // is destroyed.
    if (loaded.context) loaded.context->UnbindAll();

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
