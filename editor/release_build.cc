#include "release_build.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

#include "pack/mpack.h"
#include "scripting/as/script_builder.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

// Source code and editor files stay out of game.mpack.
bool IsSourceFile(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const char* skip :
       {".cc", ".cpp", ".cxx", ".c", ".h", ".hh", ".hpp", ".tmp"}) {
    if (ext == skip) return true;
  }
  return false;
}

bool IsSkippedAtRoot(const std::string& name) {
  return name == "CMakeLists.txt" || name == "MODULE.bazel" ||
         name == "MODULE.bazel.lock" || name == "WORKSPACE" ||
         name == "WORKSPACE.bazel" || name.starts_with("bazel-") ||
         name == "Builds";
}

std::string FormatBytes(uintmax_t bytes) {
  if (bytes < 1024) return std::format("{} B", bytes);
  if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
  if (bytes < 1024ull * 1024 * 1024) {
    return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
  }
  return std::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
}

// Bazelisk honors .bazelversion; plain bazel may not.
std::string FindBazel() {
  if (os::IsOnPath("bazelisk")) return "bazelisk";
  return "bazel";
}

// Total size of <dir>/<base>.mpack and its _NNN archives, and how many there
// are.
std::pair<uintmax_t, int> ArchiveSizes(const fs::path& dir,
                                       std::string_view base) {
  uintmax_t total = 0;
  int count = 0;
  std::error_code ec;
  for (;; count++) {
    const auto size = fs::file_size(mpack::ArchivePath(dir, base, count), ec);
    if (ec) break;
    total += size;
  }
  return {total, count};
}

}  // namespace

ReleaseBuild::~ReleaseBuild() {
  if (packing_.valid()) packing_.wait();
}

std::string ReleaseBuild::DefaultOutput(const Project& project) {
  return "Builds/" + project.GetName();
}

void ReleaseBuild::Start(const Project& project, const fs::path& output_dir,
                         uint64_t max_archive_bytes) {
  if (IsBusy()) return;
  max_archive_bytes_ = max_archive_bytes;
  project_root_ = project.GetRoot();
  project_name_ = project.GetName();
  output_dir_ = output_dir;
  cancelled_ = false;
  console_.Add(
      Console::Level::kInfo, Console::Source::kBuild,
      "Build Game: building a Release executable for " + output_dir_.string());

  config_ = MakeConfig();
  if (!fs::exists(fs::path(config_->source_dir) / "MODULE.bazel")) {
    Finish("The engine checkout this editor was built from is missing (" +
               config_->source_dir + ")",
           true);
    return;
  }
  StartBuild();
}

ReleaseBuild::Config ReleaseBuild::MakeConfig() const {
  Config config;
  config.source_dir = fs::path(MERCURY_ENGINE_DIR).string();
  config.ui_resources =
      (fs::path(config.source_dir) / "mercury" / "ui" / "resources").string();
  config.spirv_dir =
      (fs::path(config.source_dir) / "bazel-bin" / "mercury" / "shaders")
          .string();
  std::string name;
  for (char c : project_name_) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      name.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else if (!name.empty() && name.back() != '_') {
      name.push_back('_');
    }
  }
  while (!name.empty() && name.back() == '_') name.pop_back();
  config.target_name = name.empty() ? "game" : name;
  return config;
}

void ReleaseBuild::LocateExecutable() {
#if defined(_WIN32)
  const char* file = "mercury_player.exe";
#else
  const char* file = "mercury_player";
#endif
  std::error_code ec;
  for (const fs::path& dir :
       {fs::path(config_->source_dir) / "bazel-bin" / "mercury",
        fs::path(config_->source_dir) / "bazel-bin"}) {
    if (fs::exists(dir / file, ec)) {
      config_->executable = (dir / file).string();
      return;
    }
  }
}

bool ReleaseBuild::Launch(const std::vector<std::string>& args) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, Console::Source::kBuild, "> " + command);

  process_ = std::make_unique<os::Process>();
  std::string error;
  const fs::path working_dir =
      config_ ? fs::path(config_->source_dir) : project_root_;
  if (!process_->Start(args, working_dir, &error)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool ReleaseBuild::StartBuild() {
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Building a Release mercury_player with Bazel; the first one "
               "takes a while.");
  if (!Launch(
          {FindBazel(), "build", "-c", "opt", "//mercury:mercury_player"})) {
    return false;
  }
  state_ = State::kBuilding;
  return true;
}

void ReleaseBuild::StartPacking() {
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Packing assets into .mpack files...");
  state_ = State::kPacking;
  packing_ = std::async(std::launch::async, &ReleaseBuild::Pack, *config_,
                        project_root_, output_dir_, max_archive_bytes_);
}

ReleaseBuild::PackResult ReleaseBuild::Pack(Config config, fs::path root,
                                            fs::path output_dir,
                                            uint64_t max_archive_bytes) {
  PackResult result;
  auto fail = [&](std::string message) {
    result.error = std::move(message);
    return result;
  };
  // Compile the scripts in-process; the .as sources are packed with the game.
  {
    BuildOptions options;
    options.release = true;
    std::string build_log;
    const bool built = ScriptBuilder::Build(root, options, &build_log);
    std::istringstream lines(build_log);
    for (std::string line; std::getline(lines, line);)
      result.log.push_back(line);
    if (!built) return fail("building the scripts failed");
  }
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) return fail("could not create " + output_dir.string());
  const fs::path output = fs::weakly_canonical(output_dir, ec);

  // Archives from an earlier build would shadow or outlive the new ones.
  for (const auto& entry : fs::directory_iterator(output, ec)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() && (extension == mpack::kExtension ||
                                    extension == mpack::kIndexExtension)) {
      fs::remove(entry.path(), ec);
    }
  }

  // game.mpack: everything in the project but code, caches and builds.
  mpack::Writer game;
  uintmax_t game_bytes = 0;
  auto it = fs::recursive_directory_iterator(
      root, fs::directory_options::skip_permission_denied, ec);
  if (ec) return fail("could not read " + root.string() + ": " + ec.message());
  for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec)
      return fail("could not read " + root.string() + ": " + ec.message());
    const fs::path& path = it->path();
    const std::string name = path.filename().string();
    const bool is_dir = it->is_directory(ec);
    const bool skip = name.starts_with('.') ||
                      (it.depth() == 0 && IsSkippedAtRoot(name)) ||
                      (is_dir && fs::weakly_canonical(path, ec) == output);
    if (skip) {
      if (is_dir) it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec) || IsSourceFile(path)) continue;
    game.Add(path.lexically_relative(root).generic_string(), path);
    game_bytes += it->file_size(ec);
  }
  // engine.mpack: the default UI theme and fonts, and the shaders.
  mpack::Writer engine;
  uintmax_t engine_bytes = 0;
  const fs::path ui = config.ui_resources;
  if (ui.empty() || !fs::is_directory(ui, ec)) {
    return fail("the engine's UI resources are missing (" + ui.string() + ")");
  }
  for (const auto& entry : fs::recursive_directory_iterator(ui, ec)) {
    if (!entry.is_regular_file()) continue;
    engine.Add(std::string(mpack::kEngineUiFolder) +
                   entry.path().lexically_relative(ui).generic_string(),
               entry.path());
    engine_bytes += entry.file_size(ec);
  }
  const fs::path spirv = config.spirv_dir;
  int shaders = 0;
  for (const auto& entry : fs::directory_iterator(spirv, ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".spv")
      continue;
    engine.Add(std::string(mpack::kEngineShaderFolder) +
                   entry.path().filename().generic_string(),
               entry.path());
    engine_bytes += entry.file_size(ec);
    shaders++;
  }
  if (shaders == 0) {
    return fail("no compiled shaders in " + spirv.string());
  }

  std::string error;
  for (auto [writer, base, bytes] :
       {std::tuple{&game, mpack::kGamePackName, game_bytes},
        std::tuple{&engine, mpack::kEnginePackName, engine_bytes}}) {
    if (!writer->Write(output, base, max_archive_bytes, &error)) {
      return fail(error);
    }
    const auto [packed, archives] = ArchiveSizes(output, base);
    result.log.push_back(std::format(
        "{}{}: {} files, {} -> {}{}", base, mpack::kExtension,
        writer->GetEntryCount(), FormatBytes(bytes), FormatBytes(packed),
        archives > 1 ? std::format(" in {} archives", archives) : ""));
  }

  const fs::path exe = config.executable;
  const fs::path shipped =
      output / (config.target_name + exe.extension().string());
  fs::copy_file(exe, shipped, fs::copy_options::overwrite_existing, ec);
  if (ec) return fail("could not copy " + exe.string() + ": " + ec.message());
  result.log.push_back("Copied the player as " + shipped.filename().string());
  result.ok = true;
  return result;
}

void ReleaseBuild::Finish(const std::string& message, bool error) {
  console_.Add(error ? Console::Level::kError : Console::Level::kInfo,
               Console::Source::kBuild, message);
  state_ = State::kIdle;
}

void ReleaseBuild::Cancel() {
  if (state_ == State::kPacking) {
    if (!cancelled_) {
      console_.Warn("Build Game cancelled; waiting for packing to stop");
    }
    cancelled_ = true;
    return;
  }
  if (!process_) {
    state_ = State::kIdle;
    return;
  }
  process_->Kill();
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  process_.reset();
  Finish("Build Game cancelled", false);
}

void ReleaseBuild::Update() {
  if (state_ == State::kPacking) {
    if (packing_.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
      return;
    }
    PackResult result = packing_.get();
    if (cancelled_) {
      state_ = State::kIdle;
      return;
    }
    for (auto& line : result.log) {
      console_.Add(Console::Level::kInfo, Console::Source::kBuild, line);
    }
    if (!result.ok) return Finish("Build Game failed: " + result.error, true);
    return Finish("Game built to " + output_dir_.string(), false);
  }

  if (!process_) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  if (process_->IsRunning()) return;
  for (auto& line : process_->TakeOutput()) {
    console_.AddProcessOutput(Console::Source::kBuild, std::move(line));
  }
  const int code = process_->GetExitCode().value_or(-1);
  process_.reset();

  switch (state_) {
    case State::kBuilding:
      if (code != 0) return Finish("Release build failed", true);
      LocateExecutable();
      if (config_->executable.empty()) {
        return Finish("The Release build did not produce mercury_player", true);
      }
      StartPacking();
      break;
    case State::kPacking:
    case State::kIdle:
      break;
  }
}

}  // namespace Mercury
