#include "vk_gpu_timer.h"

#include <map>

#include "debugger/profiler.h"
#include "vk_check.h"

namespace Mercury::vk {

bool GpuTimer::Init(Context& context, uint32_t frames_in_flight) {
  context_ = &context;
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(context.physical_device, &properties);
  uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(context.physical_device, &family_count,
                                           families.data());
  const uint32_t bits = families[context.queue_family].timestampValidBits;
  if (bits == 0 || properties.limits.timestampPeriod <= 0.0f) return false;
  valid_mask_ = bits >= 64 ? ~0ull : (1ull << bits) - 1;
  period_ns_ = properties.limits.timestampPeriod;

  VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  info.queryCount = frames_in_flight * kMaxScopes * 2;
  if (!VK_CHECK(vkCreateQueryPool(context.device, &info, nullptr, &pool_))) return false;
  slots_.resize(frames_in_flight);
  return true;
}

void GpuTimer::Shutdown() {
  if (pool_) vkDestroyQueryPool(context_->device, pool_, nullptr);
  pool_ = VK_NULL_HANDLE;
  slots_.clear();
}

void GpuTimer::BeginFrame(VkCommandBuffer cmd, uint32_t frame_index) {
  if (!pool_) return;
  frame_index_ = frame_index;
  Slot& slot = slots_[frame_index];
  const uint32_t first = frame_index * kMaxScopes * 2;
  if (slot.used > 0) {
    std::vector<uint64_t> values(size_t{slot.used} * 2);
    // Each value with its availability: a query never written reads as 0.
    const VkResult result = vkGetQueryPoolResults(
        context_->device, pool_, first, slot.used, values.size() * sizeof(uint64_t),
        values.data(), 2 * sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result == VK_SUCCESS || result == VK_NOT_READY) {
      // The same pass several times a frame (one per camera) adds up.
      std::map<std::string_view, double> totals;
      for (uint32_t i = 0; i + 1 < slot.used; i += 2) {
        const uint64_t begin = values[2 * i], end = values[2 * (i + 1)];
        if (!values[2 * i + 1] || !values[2 * (i + 1) + 1]) continue;
        const uint64_t ticks = ((end & valid_mask_) - (begin & valid_mask_)) & valid_mask_;
        totals[slot.names[i / 2]] += static_cast<double>(ticks) * period_ns_ * 1e-6;
      }
      for (const auto& [name, ms] : totals) Profiler::AddGpu(name, ms);
    }
  }
  vkCmdResetQueryPool(cmd, pool_, first, kMaxScopes * 2);
  slot.used = 0;
  slot.names.clear();
}

int GpuTimer::Begin(VkCommandBuffer cmd, std::string_view name) {
  if (!pool_) return -1;
  Slot& slot = slots_[frame_index_];
  if (slot.used + 2 > kMaxScopes * 2) return -1;
  const int scope = static_cast<int>(slot.used);
  slot.names.push_back(name);
  slot.used += 2;
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, pool_,
                       frame_index_ * kMaxScopes * 2 + scope);
  return scope;
}

void GpuTimer::End(VkCommandBuffer cmd, int scope) {
  if (!pool_ || scope < 0) return;
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, pool_,
                       frame_index_ * kMaxScopes * 2 + scope + 1);
}

}  // namespace Mercury::vk
