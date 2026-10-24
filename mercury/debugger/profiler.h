/*
 * profiler.h
 *
 * Frame timings for finding where time goes: CPU scopes on any thread, GPU
 * pass times the renderer reports once the GPU is done with a frame, and a
 * benchmark mode for comparing builds.
 *
 *   MERCURY_PROFILE_SCOPE("prepare");   // times the rest of the block
 *
 * Environment variables (read once, at the first call):
 *   MERCURY_PROFILER=1        an overlay with the latest timings
 *   MERCURY_FIXED_DT=1        every frame advances 1/60 s, so frame N shows the
 *                             same thing however fast frames render
 *   MERCURY_BENCH_FRAMES=N    measure N frames after the warmup, print a
 *                             summary and quit
 *   MERCURY_BENCH_WARMUP=N    frames skipped once the map has loaded
 *                             (default 300: uploads, light probes settling)
 *   MERCURY_BENCH_OUT=path    also write the summary there
 */
#ifndef MERCURY_DEBUGGER_PROFILER_H
#define MERCURY_DEBUGGER_PROFILER_H

#include <chrono>
#include <cstdint>
#include <string_view>

namespace Mercury::Profiler {

// Around each frame of the main loop.
void BeginFrame();
void EndFrame();

// Frame number (BeginFrame calls so far).
uint64_t Frame();

// Whether a map is loading. The benchmark's warmup counts frames after a
// load, and each load's time is logged.
void SetLoading(bool loading);

void AddCpu(std::string_view name, double ms);
// A GPU pass of an earlier frame, measured with timestamps.
void AddGpu(std::string_view name, double ms);
// A count worth watching (draws, triangles), the latest value kept.
void SetCounter(std::string_view name, double value);

// 0, or the fixed time step in milliseconds (MERCURY_FIXED_DT).
double FixedDeltaMs();
// The benchmark measured its frames: time to quit.
bool BenchFinished();

// ImGui window with the latest timings, if MERCURY_PROFILER is set. Call
// between ImGui::NewFrame and ImGui::Render.
void DrawOverlay();

class Scope {
 public:
  explicit Scope(std::string_view name)
      : name_(name), start_(std::chrono::steady_clock::now()) {}
  ~Scope() {
    const auto end = std::chrono::steady_clock::now();
    AddCpu(name_, std::chrono::duration<double, std::milli>(end - start_).count());
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  std::string_view name_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace Mercury::Profiler

#define MERCURY_PROFILE_CONCAT_(a, b) a##b
#define MERCURY_PROFILE_CONCAT(a, b) MERCURY_PROFILE_CONCAT_(a, b)
#define MERCURY_PROFILE_SCOPE(name) \
  ::Mercury::Profiler::Scope MERCURY_PROFILE_CONCAT(mercury_profile_scope_, __LINE__)(name)

#endif  // MERCURY_DEBUGGER_PROFILER_H
