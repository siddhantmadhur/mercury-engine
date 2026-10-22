#include "vk_ui_renderer.h"

#include <algorithm>
#include <cstring>

#include "debugger/debugger.h"
#include "ui/ui_texture.h"
#include "vk_check.h"

namespace Mercury::vk {

namespace {

// Must match the register shifts in CMakeLists.txt; see ui.ps.hlsl.
constexpr uint32_t kTextureBinding = 10;  // t0
constexpr uint32_t kSamplerBinding = 20;  // s0
constexpr uint32_t kMaxTextures = 512;

}  // namespace

bool UIRenderer::Init(Context& context, VkFormat color_format,
                      VkFormat depth_format, VkShaderModule vertex_shader,
                      VkShaderModule pixel_shader, uint32_t frames_in_flight) {
  context_ = &context;
  frames_in_flight_ = frames_in_flight;
  frames_.resize(frames_in_flight);
  VkDevice device = context.device;

  VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (!VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler_))) {
    return false;
  }
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  if (!VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &nearest_sampler_))) {
    return false;
  }

  const VkDescriptorSetLayoutBinding bindings[] = {
      {kTextureBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {kSamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
       nullptr},
  };
  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = 2;
  layout_info.pBindings = bindings;
  if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &set_layout_))) {
    return false;
  }

  const VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures},
      {VK_DESCRIPTOR_TYPE_SAMPLER, kMaxTextures},
  };
  VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = kMaxTextures;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  if (!VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool_))) {
    return false;
  }

  const VkPushConstantRange push_range = {VK_SHADER_STAGE_VERTEX_BIT, 0,
                                          sizeof(float) * 2};
  VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &set_layout_;
  pipeline_layout_info.pushConstantRangeCount = 1;
  pipeline_layout_info.pPushConstantRanges = &push_range;
  if (!VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                                       &pipeline_layout_))) {
    return false;
  }
  return CreatePipeline(color_format, depth_format, vertex_shader, pixel_shader);
}

bool UIRenderer::CreatePipeline(VkFormat color_format, VkFormat depth_format,
                                VkShaderModule vs, VkShaderModule ps) {
  const VkVertexInputBindingDescription binding = {0, sizeof(UI::UIVertex),
                                                   VK_VERTEX_INPUT_RATE_VERTEX};
  const VkVertexInputAttributeDescription attributes[] = {
      {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UI::UIVertex, position)},
      {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UI::UIVertex, uv)},
      {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UI::UIVertex, color)},
      {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(UI::UIVertex, outline_color)},
      {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::UIVertex, params)},
  };
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  // Drawn over everything; no depth test or write.
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState blend = {};
  blend.blendEnable = VK_TRUE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend;
  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                           VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &color_format;
  rendering_info.depthAttachmentFormat = depth_format;

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "Main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = ps;
  stages[1].pName = "Main";

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  create_info.pNext = &rendering_info;
  create_info.stageCount = 2;
  create_info.pStages = stages;
  create_info.pVertexInputState = &vertex_input;
  create_info.pInputAssemblyState = &input_assembly;
  create_info.pViewportState = &viewport_state;
  create_info.pRasterizationState = &rasterizer;
  create_info.pMultisampleState = &multisample;
  create_info.pDepthStencilState = &depth_stencil;
  create_info.pColorBlendState = &color_blend;
  create_info.pDynamicState = &dynamic_state;
  create_info.layout = pipeline_layout_;
  return VK_CHECK(vkCreateGraphicsPipelines(context_->device, VK_NULL_HANDLE, 1,
                                            &create_info, nullptr, &pipeline_));
}

void UIRenderer::Shutdown() {
  if (!context_) return;
  VkDevice device = context_->device;
  CollectGarbage(true);
  for (auto& [id, texture] : textures_) {
    if (texture->set) vkFreeDescriptorSets(device, pool_, 1, &texture->set);
  }
  textures_.clear();
  frames_.clear();
  vkDestroyPipeline(device, pipeline_, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
  vkDestroyDescriptorPool(device, pool_, nullptr);
  vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
  vkDestroySampler(device, linear_sampler_, nullptr);
  vkDestroySampler(device, nearest_sampler_, nullptr);
  context_ = nullptr;
}

void UIRenderer::BeginFrame(uint32_t frame_index) {
  if (!context_) return;
  frame_index_ = frame_index;
  frame_counter_++;
  FrameBuffers& frame = frames_[frame_index];
  frame.vertices_used = 0;
  frame.indices_used = 0;
  frame.staging.clear();  // this frame's fence has signalled
  CollectGarbage(false);

  // Free GPU copies of textures the UI released.
  auto& registry = UI::UITextureRegistry::Instance();
  if (registry.generation() != registry_generation_) {
    registry_generation_ = registry.generation();
    for (auto it = textures_.begin(); it != textures_.end();) {
      if (it->first != 0 && !registry.Find(it->first)) {
        Retire(std::move(it->second));
        it = textures_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void UIRenderer::Retire(std::unique_ptr<GpuTexture> texture) {
  // Frames still in flight may sample it.
  garbage_.push_back({frame_counter_, std::move(texture)});
}

void UIRenderer::CollectGarbage(bool everything) {
  std::erase_if(garbage_, [&](Garbage& g) {
    if (!everything && frame_counter_ < g.frame + frames_in_flight_ + 1) return false;
    if (g.texture->set) vkFreeDescriptorSets(context_->device, pool_, 1, &g.texture->set);
    return true;
  });
}

bool UIRenderer::EnsureCapacity(vk::Buffer& buffer, VkDeviceSize needed,
                                VkBufferUsageFlags usage) {
  if (buffer.Valid() && buffer.Size() >= needed) return true;
  VkDeviceSize size = std::max<VkDeviceSize>(buffer.Valid() ? buffer.Size() : 0, 64 * 1024);
  while (size < needed) size *= 2;
  vk::Buffer bigger;
  if (!bigger.Create(context_->allocator, size, usage, vk::Buffer::Memory::kHostVisible)) {
    PDebug::error("Vulkan: could not grow the UI buffer to {} bytes", size);
    return false;
  }
  if (buffer.Valid()) {
    // Batches prepared earlier this frame still point at the old buffer, so
    // keep it (and its contents) until this frame's fence signals.
    std::memcpy(bigger.Mapped(), buffer.Mapped(), buffer.Size());
    frames_[frame_index_].staging.push_back(std::move(buffer));
  }
  buffer = std::move(bigger);
  return true;
}

bool UIRenderer::Upload(VkCommandBuffer cmd, GpuTexture& texture, const uint8_t* rgba,
                        int width, int height) {
  const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
  vk::Buffer staging;
  if (!staging.Create(context_->allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      vk::Buffer::Memory::kHostVisible) ||
      !staging.Write(rgba, bytes)) {
    return false;
  }
  const bool fresh = texture.version == 0;
  // Earlier frames may still sample the image; the barrier waits for them.
  vk::TransitionImage(cmd, texture.image.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                      fresh ? VK_IMAGE_LAYOUT_UNDEFINED
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  vkCmdCopyBufferToImage(cmd, staging.Get(), texture.image.Get(),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  vk::TransitionImage(cmd, texture.image.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  frames_[frame_index_].staging.push_back(std::move(staging));
  return true;
}

UIRenderer::GpuTexture* UIRenderer::SyncTexture(VkCommandBuffer cmd, uint32_t id) {
  std::shared_ptr<UI::UITexture> cpu;
  static const uint8_t kWhite[4] = {255, 255, 255, 255};
  const uint8_t* pixels = kWhite;
  int width = 1, height = 1;
  uint64_t version = 1;
  bool linear = true;
  if (id != 0) {
    cpu = UI::UITextureRegistry::Instance().Find(id);
    if (!cpu || cpu->width <= 0 || cpu->height <= 0 || cpu->rgba.empty()) {
      return id == 0 ? nullptr : SyncTexture(cmd, 0);
    }
    pixels = cpu->rgba.data();
    width = cpu->width;
    height = cpu->height;
    version = cpu->version;
    linear = cpu->linear_filter;
  }

  auto it = textures_.find(id);
  GpuTexture* texture = it == textures_.end() ? nullptr : it->second.get();
  if (texture && texture->version == version) return texture;

  if (texture && (texture->width != width || texture->height != height ||
                  texture->linear != linear)) {
    Retire(std::move(it->second));
    textures_.erase(it);
    texture = nullptr;
  }
  if (!texture) {
    auto created = std::make_unique<GpuTexture>();
    if (!created->image.Create(context_->allocator, context_->device,
                               static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                               VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT)) {
      PDebug::error("Vulkan: could not create {}x{} UI texture", width, height);
      return nullptr;
    }
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &set_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(context_->device, &alloc_info, &created->set))) {
      return nullptr;
    }
    VkDescriptorImageInfo image_info = {VK_NULL_HANDLE, created->image.View(),
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo sampler_info = {linear ? linear_sampler_ : nearest_sampler_,
                                          VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[2] = {};
    for (auto& write : writes) {
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = created->set;
      write.descriptorCount = 1;
    }
    writes[0].dstBinding = kTextureBinding;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &image_info;
    writes[1].dstBinding = kSamplerBinding;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &sampler_info;
    vkUpdateDescriptorSets(context_->device, 2, writes, 0, nullptr);
    created->width = width;
    created->height = height;
    created->linear = linear;
    texture = created.get();
    textures_[id] = std::move(created);
  }
  if (!Upload(cmd, *texture, pixels, width, height)) return nullptr;
  texture->version = version;
  return texture;
}

UIRenderer::Batch UIRenderer::Prepare(VkCommandBuffer cmd, const UI::UIDrawData& data) {
  Batch batch;
  if (!context_ || data.empty()) return batch;
  for (const auto& command : data.commands) SyncTexture(cmd, command.texture_id);
  SyncTexture(cmd, 0);

  FrameBuffers& frame = frames_[frame_index_];
  const VkDeviceSize vertex_bytes = data.vertices.size() * sizeof(UI::UIVertex);
  const VkDeviceSize index_bytes = data.indices.size() * sizeof(uint32_t);
  if (!EnsureCapacity(frame.vertices, frame.vertices_used + vertex_bytes,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
      !EnsureCapacity(frame.indices, frame.indices_used + index_bytes,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
    return batch;
  }
  frame.vertices.Write(data.vertices.data(), vertex_bytes, frame.vertices_used);
  frame.indices.Write(data.indices.data(), index_bytes, frame.indices_used);
  batch.data = &data;
  batch.vertex_buffer = frame.vertices.Get();
  batch.index_buffer = frame.indices.Get();
  batch.vertex_offset = frame.vertices_used;
  batch.index_offset = frame.indices_used;
  frame.vertices_used += vertex_bytes;
  frame.indices_used += index_bytes;
  return batch;
}

void UIRenderer::Record(VkCommandBuffer cmd, const Batch& batch, VkExtent2D extent) {
  if (!batch.valid() || extent.width == 0 || extent.height == 0) return;
  const UI::UIDrawData& data = *batch.data;

  VkViewport viewport = {0.0f, 0.0f, static_cast<float>(extent.width),
                         static_cast<float>(extent.height), 0.0f, 1.0f};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  const float inv_size[2] = {1.0f / extent.width, 1.0f / extent.height};
  vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(inv_size), inv_size);
  vkCmdBindVertexBuffers(cmd, 0, 1, &batch.vertex_buffer, &batch.vertex_offset);
  vkCmdBindIndexBuffer(cmd, batch.index_buffer, batch.index_offset, VK_INDEX_TYPE_UINT32);

  VkDescriptorSet bound = VK_NULL_HANDLE;
  for (const auto& command : data.commands) {
    if (command.index_count == 0) continue;
    auto it = textures_.find(command.texture_id);
    if (it == textures_.end()) it = textures_.find(0);
    if (it == textures_.end()) continue;
    if (it->second->set != bound) {
      bound = it->second->set;
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0,
                              1, &bound, 0, nullptr);
    }
    int x = 0, y = 0;
    int w = static_cast<int>(extent.width), h = static_cast<int>(extent.height);
    if (command.clip.z >= 0) {
      x = std::clamp(command.clip.x, 0, w);
      y = std::clamp(command.clip.y, 0, h);
      w = std::clamp(command.clip.x + command.clip.z, 0, w) - x;
      h = std::clamp(command.clip.y + command.clip.w, 0, h) - y;
    }
    if (w <= 0 || h <= 0) continue;
    const VkRect2D scissor = {{x, y}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDrawIndexed(cmd, command.index_count, 1, command.index_offset, 0, 0);
  }
  // Leave the scissor covering the whole target for whatever draws next.
  const VkRect2D full = {{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &full);
}

}  // namespace Mercury::vk
