#include "release_build.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>

#include "pack/mpack.h"
#include "serialization/mtext.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

// What the release configure names its .mercury/<name>.mcfg, so it doesn't
// replace the Play build's player.mcfg.
constexpr std::string_view kReleaseConfig = "player-release";
constexpr std::string_view kPlayConfig = "player";

// Source code and editor files stay out of game.mpack.
bool IsSourceFile(const fs::path& path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const char* skip : {".cc", ".cpp", ".cxx", ".c", ".h", ".hh", ".hpp",
                           ".tmp"}) {
    if (ext == skip) return true;
  }
  return false;
}

bool IsSkippedAtRoot(const std::string& name) {
  return name == "scripts" || name == "CMakeLists.txt" || name == "Builds" ||
         name == "imgui.ini";
}

std::string FormatBytes(uintmax_t bytes) {
  if (bytes < 1024) return std::format("{} B", bytes);
  if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
  if (bytes < 1024ull * 1024 * 1024) {
    return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
  }
  return std::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
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

// The top-level source folder of an existing CMake build.
std::string SourceDirOf(const fs::path& build_dir) {
  std::ifstream cache(build_dir / "CMakeCache.txt");
  constexpr std::string_view kKey = "CMAKE_HOME_DIRECTORY:INTERNAL=";
  std::string line;
  while (std::getline(cache, line)) {
    if (line.starts_with(kKey)) return line.substr(kKey.size());
  }
  return "";
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
  output_dir_ = output_dir;
  cancelled_ = false;
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Build Game: building a Release executable for " +
                   output_dir_.string());

  config_ = ReadConfig(kReleaseConfig);
  if (config_) {
    StartBuild();
  } else {
    StartConfigure();
  }
}

std::optional<ReleaseBuild::Config> ReleaseBuild::ReadConfig(
    std::string_view name) const {
  const fs::path cache = project_root_ / kProjectCacheDir;
  const std::string base(name);
  for (const auto& file :
       {cache / (base + ".mcfg"), cache / (base + "-Release.mcfg"),
        cache / (base + "-Debug.mcfg"), cache / (base + "-RelWithDebInfo.mcfg")}) {
    mtext::Document document;
    if (!mtext::LoadFile(file, document)) continue;
    auto section = document.Find("player");
    if (!section) continue;
    Config config;
    config.executable = section->properties.GetString("executable");
    config.build_dir = section->properties.GetString("build_dir");
    config.source_dir = section->properties.GetString("source_dir");
    config.target = section->properties.GetString("target");
    config.spirv_dir = section->properties.GetString("spirv_dir");
    config.ui_resources = section->properties.GetString("ui_resources");
    if (!config.executable.empty() && !config.build_dir.empty()) return config;
  }
  return std::nullopt;
}

bool ReleaseBuild::Launch(const std::vector<std::string>& args) {
  std::string command;
  for (const auto& arg : args) command += (command.empty() ? "" : " ") + arg;
  console_.Add(Console::Level::kInfo, Console::Source::kBuild, "> " + command);

  process_ = std::make_unique<os::Process>();
  std::string error;
  if (!process_->Start(args, project_root_, &error)) {
    process_.reset();
    Finish(error, true);
    return false;
  }
  return true;
}

bool ReleaseBuild::StartConfigure() {
  // Configure the same CMake project the Play build came from: the project
  // itself, or for the engine's examples the engine checkout.
  fs::path source = project_root_;
  if (auto play = ReadConfig(kPlayConfig)) {
    source = !play->source_dir.empty() ? fs::path(play->source_dir)
                                       : fs::path(SourceDirOf(play->build_dir));
  }
  if (source.empty() || !fs::exists(source / "CMakeLists.txt")) {
    Finish(
        "Build Game needs a CMake project; press Build once so "
        ".mercury/player.mcfg exists",
        true);
    return false;
  }
  console_.Add(Console::Level::kInfo, Console::Source::kBuild,
               "Configuring a Release build; the first one takes a while.");
  std::vector<std::string> args = {
      "cmake", "-S", source.string(), "-B",
      (project_root_ / kProjectCacheDir / "build-release").string(),
      "-DCMAKE_BUILD_TYPE=Release",
      "-DMERCURY_PLAYER_MCFG=" + std::string(kReleaseConfig)};
  std::error_code ec;
  if (!fs::equivalent(source, project_root_, ec)) {
    // Building inside the engine checkout: skip what the game doesn't need.
    args.push_back("-DMERCURY_BUILD_TESTS=OFF");
    args.push_back("-DMERCURY_BUILD_EDITOR=OFF");
  }
  if (os::IsOnPath("ninja")) {
    args.insert(args.begin() + 1, {"-G", "Ninja"});
  }
  if (!Launch(args)) return false;
  state_ = State::kConfiguring;
  return true;
}

bool ReleaseBuild::StartBuild() {
  if (!Launch({"cmake", "--build", config_->build_dir, "--target",
               config_->target, "--config", "Release"})) {
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
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) return fail("could not create " + output_dir.string());
  const fs::path output = fs::weakly_canonical(output_dir, ec);

  // Archives from an earlier build would shadow or outlive the new ones.
  for (const auto& entry : fs::directory_iterator(output, ec)) {
    const auto extension = entry.path().extension();
    if (entry.is_regular_file() &&
        (extension == mpack::kExtension || extension == mpack::kIndexExtension)) {
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
    if (ec) return fail("could not read " + root.string() + ": " + ec.message());
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
    if (!entry.is_regular_file() || entry.path().extension() != ".spv") continue;
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
  fs::copy_file(exe, output / exe.filename(),
                fs::copy_options::overwrite_existing, ec);
  if (ec) return fail("could not copy " + exe.string() + ": " + ec.message());
  result.log.push_back("Copied " + exe.filename().string());
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
    if (packing_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
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
    case State::kConfiguring:
      if (code != 0) return Finish("CMake configure (Release) failed", true);
      config_ = ReadConfig(kReleaseConfig);
      if (!config_) {
        return Finish(
            "CMake ran but did not write .mercury/player-release.mcfg; does "
            "CMakeLists.txt call mercury_add_game()?",
            true);
      }
      StartBuild();
      break;
    case State::kBuilding:
      if (code != 0) return Finish("Release build failed", true);
      StartPacking();
      break;
    case State::kPacking:
    case State::kIdle:
      break;
  }
}

}  // namespace Mercury
