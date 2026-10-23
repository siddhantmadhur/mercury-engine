#include "vk_buffer.h"

#include <algorithm>
#include <utility>
#include <cstring>

#include "vk_check.h"
#include "vk_context.h"

namespace Mercury::vk {

Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    Reset();
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    mapped_ = std::exchange(other.mapped_, nullptr);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

bool Buffer::Create(VmaAllocator allocator, VkDeviceSize size,
                    VkBufferUsageFlags usage, Memory memory) {
  Reset();

  VkBufferCreateInfo buffer_info = {.sType =
                                        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info = {};
  if (memory == Memory::kHostVisible) {
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;
  } else {
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }

  VmaAllocationInfo result = {};
  if (!VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer_,
                                &allocation_, &result))) {
    return false;
  }
  allocator_ = allocator;
  mapped_ = result.pMappedData;
  size_ = size;
  return true;
}

void Buffer::Reset() {
  if (buffer_ != VK_NULL_HANDLE) {
    vmaDestroyBuffer(allocator_, buffer_, allocation_);
  }
  buffer_ = VK_NULL_HANDLE;
  allocation_ = VK_NULL_HANDLE;
  mapped_ = nullptr;
  size_ = 0;
}

bool Buffer::Write(const void* data, VkDeviceSize bytes, VkDeviceSize offset) {
  if (mapped_ == nullptr || offset + bytes > size_) return false;
  if (bytes == 0) return true;
  std::memcpy(static_cast<uint8_t*>(mapped_) + offset, data, bytes);
  Flush(offset, bytes);
  return true;
}

void Buffer::Flush(VkDeviceSize offset, VkDeviceSize bytes) {
  vmaFlushAllocation(allocator_, allocation_, offset, bytes);
}

bool Image::Create(VmaAllocator allocator, VkDevice device, uint32_t width,
                   uint32_t height, VkFormat format, VkImageUsageFlags usage,
                   VkImageAspectFlags aspect, uint32_t mip_levels) {
  Reset();
  mip_levels_ = std::max(1u, mip_levels);
  aspect_ = aspect;

  VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = format;
  image_info.extent = {width, height, 1};
  image_info.mipLevels = mip_levels_;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (!VK_CHECK(vmaCreateImage(allocator, &image_info, &alloc_info, &image_,
                               &allocation_, nullptr))) {
    return false;
  }
  allocator_ = allocator;
  device_ = device;
  extent_ = {width, height};
  format_ = format;

  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = image_;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = format;
  view_info.subresourceRange = {aspect, 0, mip_levels_, 0, 1};
  return VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &view_));
}

VkImageView Image::MipView(uint32_t level) {
  if (level >= mip_levels_ || image_ == VK_NULL_HANDLE) return VK_NULL_HANDLE;
  if (mip_views_.empty()) mip_views_.resize(mip_levels_, VK_NULL_HANDLE);
  if (mip_views_[level] == VK_NULL_HANDLE) {
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange = {aspect_, level, 1, 0, 1};
    vkCreateImageView(device_, &view_info, nullptr, &mip_views_[level]);
  }
  return mip_views_[level];
}

Image& Image::operator=(Image&& other) noexcept {
  if (this == &other) return *this;
  Reset();
  allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
  device_ = std::exchange(other.device_, VK_NULL_HANDLE);
  image_ = std::exchange(other.image_, VK_NULL_HANDLE);
  view_ = std::exchange(other.view_, VK_NULL_HANDLE);
  allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
  extent_ = std::exchange(other.extent_, VkExtent2D{});
  format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
  mip_views_ = std::move(other.mip_views_);
  other.mip_views_.clear();
  aspect_ = other.aspect_;
  mip_levels_ = std::exchange(other.mip_levels_, 1u);
  return *this;
}

void Image::Reset() {
  for (VkImageView view : mip_views_) {
    if (view != VK_NULL_HANDLE) vkDestroyImageView(device_, view, nullptr);
  }
  mip_views_.clear();
  if (view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view_, nullptr);
  if (image_ != VK_NULL_HANDLE) {
    vmaDestroyImage(allocator_, image_, allocation_);
  }
  view_ = VK_NULL_HANDLE;
  image_ = VK_NULL_HANDLE;
  allocation_ = VK_NULL_HANDLE;
  extent_ = {};
  format_ = VK_FORMAT_UNDEFINED;
}

void TransitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageAspectFlags aspect, VkImageLayout old_layout,
                     VkImageLayout new_layout, VkPipelineStageFlags2 src_stage,
                     VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                     VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1};

  VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

void TransitionImageMips(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                         uint32_t base_mip, uint32_t mip_count,
                         VkImageLayout old_layout, VkImageLayout new_layout,
                         VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                         VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {aspect, base_mip, mip_count, 0, 1};
  VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

void GlobalBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage,
                   VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                   VkAccessFlags2 dst_access) {
  VkMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
  barrier.srcStageMask = src_stage;
  barrier.srcAccessMask = src_access;
  barrier.dstStageMask = dst_stage;
  barrier.dstAccessMask = dst_access;

  VkDependencyInfo dependency = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dependency);
}

}  // namespace Mercury::vk
