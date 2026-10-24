#ifndef MERCURY_ENGINE_VK_BUFFER_H
#define MERCURY_ENGINE_VK_BUFFER_H

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace Mercury::vk {

/**
 * @brief VMA-backed VkBuffer that frees itself when destroyed or reset
 *
 * Host-visible buffers are persistently mapped; write through Mapped() and
 * call Flush() afterwards (a no-op on host-coherent memory).
 */
class Buffer {
 public:
  enum class Memory {
    kDeviceLocal,  // GPU-only; filled with transfer commands
    kHostVisible,  // persistently mapped, written sequentially by the CPU
    kReadback,     // persistently mapped, read by the CPU (Invalidate first)
  };

  Buffer() = default;
  ~Buffer() { Reset(); }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&& other) noexcept { *this = std::move(other); }
  Buffer& operator=(Buffer&& other) noexcept;

  bool Create(VmaAllocator allocator, VkDeviceSize size,
              VkBufferUsageFlags usage, Memory memory);
  void Reset();

  // Copies `bytes` bytes to `offset` in the mapped memory and flushes them.
  // Returns false (and copies nothing) if the write would overrun the buffer.
  bool Write(const void* data, VkDeviceSize bytes, VkDeviceSize offset = 0);
  void Flush(VkDeviceSize offset, VkDeviceSize bytes);
  // Makes what the GPU wrote visible to Mapped() (a no-op on coherent memory).
  void Invalidate();

  VkBuffer Get() const { return buffer_; }
  void* Mapped() const { return mapped_; }
  VkDeviceSize Size() const { return size_; }
  bool Valid() const { return buffer_ != VK_NULL_HANDLE; }

 private:
  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  void* mapped_ = nullptr;
  VkDeviceSize size_ = 0;
};

/**
 * @brief VMA-backed 2D VkImage (or 2D array) with a view of all of it
 */
class Image {
 public:
  Image() = default;
  ~Image() { Reset(); }
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  Image(Image&& other) noexcept { *this = std::move(other); }
  Image& operator=(Image&& other) noexcept;

  bool Create(VmaAllocator allocator, VkDevice device, uint32_t width,
              uint32_t height, VkFormat format, VkImageUsageFlags usage,
              VkImageAspectFlags aspect, uint32_t mip_levels = 1);
  // An array of `layers` images; View() sees them all (a 2D array view).
  bool CreateArray(VmaAllocator allocator, VkDevice device, uint32_t width, uint32_t height,
                   uint32_t layers, VkFormat format, VkImageUsageFlags usage,
                   VkImageAspectFlags aspect);
  void Reset();

  VkImage Get() const { return image_; }
  VkImageView View() const { return view_; }
  VkExtent2D Extent() const { return extent_; }
  VkFormat Format() const { return format_; }
  uint32_t MipLevels() const { return mip_levels_; }
  // A view of one mip level (created on first use).
  VkImageView MipView(uint32_t level);
  uint32_t Layers() const { return layers_; }
  // A 2D view of one array layer (created on first use), to render into.
  VkImageView LayerView(uint32_t layer);

 private:
  bool CreateImage(VmaAllocator allocator, VkDevice device, uint32_t width, uint32_t height,
                   uint32_t layers, VkFormat format, VkImageUsageFlags usage,
                   VkImageAspectFlags aspect, uint32_t mip_levels, VkImageViewType view_type);
  std::vector<VkImageView> mip_views_;
  std::vector<VkImageView> layer_views_;
  uint32_t layers_ = 1;
  VkImageAspectFlags aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
  uint32_t mip_levels_ = 1;
  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  VkExtent2D extent_ = {};
  VkFormat format_ = VK_FORMAT_UNDEFINED;
};

/**
 * @brief Records a single-subresource image layout transition (sync2)
 */
void TransitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageAspectFlags aspect, VkImageLayout old_layout,
                     VkImageLayout new_layout,
                     VkPipelineStageFlags2 src_stage,
                     VkAccessFlags2 src_access,
                     VkPipelineStageFlags2 dst_stage,
                     VkAccessFlags2 dst_access);

// The same for a range of mip levels.
void TransitionImageMips(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                         uint32_t base_mip, uint32_t mip_count,
                         VkImageLayout old_layout, VkImageLayout new_layout,
                         VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                         VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);

/**
 * @brief Records a global memory barrier (sync2)
 */
void GlobalBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage,
                   VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                   VkAccessFlags2 dst_access);

}  // namespace Mercury::vk

#endif  // MERCURY_ENGINE_VK_BUFFER_H
