#include "profiler.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "debugger/debugger.h"
#include "imgui.h"

namespace Mercury::Profiler {

namespace {

struct Stat {
  double frame = 0.0;     // this frame so far
  double last = 0.0;      // the last finished frame
  double smooth = 0.0;    // averaged over recent frames, for the overlay
  double bench_sum = 0.0;
  double bench_max = 0.0;
  bool gpu = false;
};

struct Config {
  bool overlay = false;
  double fixed_dt = 0.0;
  uint64_t bench_frames = 0;
  uint64_t bench_warmup = 300;
  std::string bench_out;
};

uint64_t EnvNumber(const char* name, uint64_t fallback) {
  const char* value = std::getenv(name);
  return value ? std::strtoull(value, nullptr, 10) : fallback;
}

const Config& GetConfig() {
  static const Config config = [] {
    Config c;
    c.overlay = EnvNumber("MERCURY_PROFILER", 0) != 0;
    c.fixed_dt = EnvNumber("MERCURY_FIXED_DT", 0) != 0 ? 1000.0 / 60.0 : 0.0;
    c.bench_frames = EnvNumber("MERCURY_BENCH_FRAMES", 0);
    c.bench_warmup = EnvNumber("MERCURY_BENCH_WARMUP", 300);
    if (const char* out = std::getenv("MERCURY_BENCH_OUT")) c.bench_out = out;
    return c;
  }();
  return config;
}

struct State {
  std::mutex mutex;
  std::map<std::string, Stat, std::less<>> stats;
  std::map<std::string, double, std::less<>> counters;
  uint64_t frame = 0;
  std::chrono::steady_clock::time_point frame_start;
  bool started = false;
  double last_frame_ms = 0.0;
  double smooth_frame_ms = 0.0;
  std::vector<double> bench_frame_ms;
  bool bench_done = false;
  // Frames since the last map load finished, and when it started.
  bool loading = true;
  uint64_t ready_frames = 0;
  std::chrono::steady_clock::time_point load_start = std::chrono::steady_clock::now();
  double last_load_ms = 0.0;
};

bool Measured(const State& s) {
  const Config& config = GetConfig();
  return config.bench_frames > 0 && !s.bench_done && s.ready_frames > config.bench_warmup;
}

State& GetState() {
  static State state;
  return state;
}

void Add(std::string_view name, double ms, bool gpu) {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  auto it = s.stats.find(name);
  if (it == s.stats.end()) it = s.stats.emplace(std::string(name), Stat{}).first;
  it->second.frame += ms;
  it->second.gpu = gpu;
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const size_t i = std::min(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5));
  return values[i];
}

void ReportBench(State& s) {
  const Config& config = GetConfig();
  const auto& frames = s.bench_frame_ms;
  double total = 0.0;
  for (double ms : frames) total += ms;
  const double n = static_cast<double>(frames.size());
  const double average = total / std::max(n, 1.0);
  std::ostringstream out;
  out << std::format("load {:.0f} ms\n", s.last_load_ms);
  out << std::format("frames {}  avg {:.3f} ms ({:.1f} fps)  p50 {:.3f}  p95 {:.3f}  p99 {:.3f}  max {:.3f}\n",
                     frames.size(), average, 1000.0 / std::max(average, 1e-6),
                     Percentile(frames, 0.5), Percentile(frames, 0.95),
                     Percentile(frames, 0.99), Percentile(frames, 1.0));
  for (int pass = 0; pass < 2; pass++) {
    for (const auto& [name, stat] : s.stats) {
      if (stat.gpu != (pass == 1)) continue;
      out << std::format("{} {:<28} avg {:8.3f} ms  max {:8.3f} ms\n", stat.gpu ? "gpu" : "cpu",
                         name, stat.bench_sum / std::max(n, 1.0), stat.bench_max);
    }
  }
  for (const auto& [name, value] : s.counters) {
    out << std::format("count {:<26} {:.0f}\n", name, value);
  }
  PDebug::info("Benchmark:\n{}", out.str());
  if (!config.bench_out.empty()) {
    std::ofstream file(config.bench_out);
    file << out.str();
  }
}

}  // namespace

void BeginFrame() {
  State& s = GetState();
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(s.mutex);
  if (s.started) {
    s.last_frame_ms = std::chrono::duration<double, std::milli>(now - s.frame_start).count();
    s.smooth_frame_ms = s.smooth_frame_ms * 0.95 + s.last_frame_ms * 0.05;
    const Config& config = GetConfig();
    // The frame that just ended counts if it was measured.
    if (Measured(s)) {
      s.bench_frame_ms.push_back(s.last_frame_ms);
      if (s.bench_frame_ms.size() >= config.bench_frames) {
        s.bench_done = true;
        ReportBench(s);
      }
    }
  }
  s.started = true;
  s.frame_start = now;
  s.frame++;
  if (!s.loading) s.ready_frames++;
}

void EndFrame() {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  const bool measured = Measured(s);
  for (auto& [name, stat] : s.stats) {
    stat.last = stat.frame;
    stat.smooth = stat.smooth * 0.95 + stat.frame * 0.05;
    if (measured) {
      stat.bench_sum += stat.frame;
      stat.bench_max = std::max(stat.bench_max, stat.frame);
    }
    stat.frame = 0.0;
  }
}

uint64_t Frame() {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  return s.frame;
}

void SetLoading(bool loading) {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  if (loading == s.loading) return;
  const auto now = std::chrono::steady_clock::now();
  s.loading = loading;
  if (loading) {
    s.load_start = now;
  } else {
    s.last_load_ms = std::chrono::duration<double, std::milli>(now - s.load_start).count();
    s.ready_frames = 0;
    PDebug::info("Profiler: map loaded in {:.0f} ms", s.last_load_ms);
  }
}

void AddCpu(std::string_view name, double ms) { Add(name, ms, false); }
void AddGpu(std::string_view name, double ms) { Add(name, ms, true); }

void SetCounter(std::string_view name, double value) {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  auto it = s.counters.find(name);
  if (it == s.counters.end()) it = s.counters.emplace(std::string(name), 0.0).first;
  it->second = value;
}

double FixedDeltaMs() { return GetConfig().fixed_dt; }

bool BenchFinished() {
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  return s.bench_done;
}

void DrawOverlay() {
  if (!GetConfig().overlay) return;
  State& s = GetState();
  std::lock_guard lock(s.mutex);
  ImGui::SetNextWindowBgAlpha(0.6f);
  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Profiler", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::Text("frame %.2f ms (%.0f fps)", s.smooth_frame_ms,
              1000.0 / std::max(s.smooth_frame_ms, 1e-6));
  for (int pass = 0; pass < 2; pass++) {
    ImGui::Separator();
    ImGui::TextUnformatted(pass == 0 ? "CPU" : "GPU");
    for (const auto& [name, stat] : s.stats) {
      if (stat.gpu != (pass == 1)) continue;
      ImGui::Text("  %-26s %7.3f ms", name.c_str(), stat.smooth);
    }
  }
  if (!s.counters.empty()) {
    ImGui::Separator();
    for (const auto& [name, value] : s.counters) {
      ImGui::Text("  %-26s %.0f", name.c_str(), value);
    }
  }
  ImGui::End();
}

}  // namespace Mercury::Profiler
