/*
 * vk_gpu_timer.h
 *
 * GPU time of named passes, from timestamp queries. Results are read when
 * the frame slot comes around again (after its fence), and go to the
 * profiler.
 *
 *   timer.BeginFrame(cmd, frame_index);  // after the frame's fence
 *   {
 *     GpuScope scope(timer, cmd, "shadows");
 *     ...
 *   }
 */
#ifndef MERCURY_ENGINE_VK_GPU_TIMER_H
#define MERCURY_ENGINE_VK_GPU_TIMER_H

#include <vulkan/vulkan.h>

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "vk_context.h"

namespace Mercury::vk {

class GpuTimer {
 public:
  static constexpr uint32_t kMaxScopes = 64;

  GpuTimer() = default;
  ~GpuTimer() { Shutdown(); }
  GpuTimer(const GpuTimer&) = delete;
  GpuTimer& operator=(const GpuTimer&) = delete;

  bool Init(Context& context, uint32_t frames_in_flight);
  void Shutdown();

  // Reports the slot's previous results and resets its queries. Outside
  // any rendering pass.
  void BeginFrame(VkCommandBuffer cmd, uint32_t frame_index);

  // The latest milliseconds measured for a scope name (0 if none yet).
  double Last(std::string_view name) const;

  // A scope's index, or -1 when timing is unavailable or full.
  int Begin(VkCommandBuffer cmd, std::string_view name);
  void End(VkCommandBuffer cmd, int scope);

 private:
  struct Slot {
    std::vector<std::string_view> names;  // string literals
    uint32_t used = 0;                    // queries written
  };

  Context* context_ = nullptr;
  VkQueryPool pool_ = VK_NULL_HANDLE;
  double period_ns_ = 1.0;
  uint64_t valid_mask_ = ~0ull;
  uint32_t frame_index_ = 0;
  std::vector<Slot> slots_;
  std::map<std::string_view, double> last_;
};

// Times the rest of the block on the GPU.
class GpuScope {
 public:
  GpuScope(GpuTimer* timer, VkCommandBuffer cmd, std::string_view name)
      : timer_(timer), cmd_(cmd), scope_(timer ? timer->Begin(cmd, name) : -1) {}
  ~GpuScope() {
    if (timer_) timer_->End(cmd_, scope_);
  }
  GpuScope(const GpuScope&) = delete;
  GpuScope& operator=(const GpuScope&) = delete;

 private:
  GpuTimer* timer_;
  VkCommandBuffer cmd_;
  int scope_;
};

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_GPU_TIMER_H
