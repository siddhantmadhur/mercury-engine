// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "vk_scene_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "components/components.h"
#include "debugger/debugger.h"
#include "debugger/profiler.h"
#include "glm/ext/matrix_transform.hpp"
#include "properties/transform.h"
#include "scene/map_scene.h"
#include "vk_check.h"

namespace Mercury::vk {

namespace {

constexpr uint32_t kMaxPasses = 64;
constexpr VkFormat kRenderTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr uint32_t kNoSkin = 0xffffffffu;
constexpr uint32_t kFlagSdfText = 2;
constexpr uint32_t kFlagClampTexture = 4;

constexpr VkPipelineStageFlags2 kDepthStages =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
constexpr VkAccessFlags2 kColorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr VkAccessFlags2 kDepthAccess =
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

struct FrontFace {
  int current = -1;
  void Set(VkCommandBuffer cmd, bool mirrored) {
    if (current == static_cast<int>(mirrored)) return;
    current = mirrored;
    vkCmdSetFrontFace(cmd, mirrored ? VK_FRONT_FACE_CLOCKWISE
                                    : VK_FRONT_FACE_COUNTER_CLOCKWISE);
  }
};

float SrgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

glm::vec3 SrgbToLinear(glm::vec3 c) {
  return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)};
}

bool IsSrgbFormat(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_SRGB ||
         format == VK_FORMAT_B8G8R8A8_SRGB;
}

void SetViewport(VkCommandBuffer cmd, VkExtent2D extent) {
  VkViewport viewport = {};
  viewport.y = static_cast<float>(extent.height);
  viewport.width = static_cast<float>(extent.width);
  viewport.height = -static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  const VkRect2D scissor = {{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &scissor);
}

}  // namespace

// -------------------------------------------------------------------- Init --

bool SceneRenderer::Init(Context& context, uint32_t frames_in_flight,
                         const ShaderLoader& shaders) {
  context_ = &context;
  frames_in_flight_ = frames_in_flight;

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(context.physical_device, &properties);
  const VkDeviceSize alignment =
      properties.limits.minUniformBufferOffsetAlignment;
  uniform_stride_ =
      (sizeof(FrameUniforms) + alignment - 1) / alignment * alignment;

  model_vs_ = shaders("model.vs.spv");
  model_ps_ = shaders("model.ps.spv");
  skybox_vs_ = shaders("skybox.vs.spv");
  skybox_ps_ = shaders("skybox.ps.spv");
  if (!model_vs_ || !model_ps_ || !skybox_vs_ || !skybox_ps_) {
    PDebug::error("Vulkan: failed to load basic model/skybox shaders");
    Shutdown();
    return false;
  }

  if (!CreateLayouts() || !CreateFrameResources()) {
    PDebug::error("Vulkan: could not create the model renderer");
    Shutdown();
    return false;
  }

  if (!vertex_buffer_.Create(context.allocator, sizeof(ModelVertex) * 262144,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             Buffer::Memory::kDeviceLocal) ||
      !index_buffer_.Create(context.allocator, sizeof(uint32_t) * 1048576,
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            Buffer::Memory::kDeviceLocal)) {
    Shutdown();
    return false;
  }
  vertex_capacity_ = 262144;
  index_capacity_ = 1048576;
  return true;
}

bool SceneRenderer::CreateLayouts() {
  VkDevice device = context_->device;
  {
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
         nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(std::size(bindings));
    info.pBindings = bindings;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr,
                                              &frame_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorSetLayoutBinding binding = {
        0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures,
        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_info.bindingCount = 1;
    flags_info.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = &flags_info;
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    info.bindingCount = 1;
    info.pBindings = &binding;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr,
                                              &texture_layout_))) {
      return false;
    }
  }
  {
    // Matches skybox.ps.hlsl: t0 -> binding 10, s0 -> binding 20.
    const VkDescriptorSetLayoutBinding bindings[] = {
        {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
        {20, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         nullptr},
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(std::size(bindings));
    info.pBindings = bindings;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr,
                                              &skybox_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorSetLayout layouts[] = {frame_layout_, texture_layout_};
    VkPipelineLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 2;
    info.pSetLayouts = layouts;
    if (!VK_CHECK(
            vkCreatePipelineLayout(device, &info, nullptr, &model_layout_))) {
      return false;
    }
  }
  {
    const VkPushConstantRange push_range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                            sizeof(glm::mat4)};
    VkPipelineLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 1;
    info.pSetLayouts = &skybox_layout_;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &push_range;
    if (!VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr,
                                         &skybox_pipeline_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frames_in_flight_},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames_in_flight_ * 2},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 16},
    };
    VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = frames_in_flight_ + 4;
    info.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    info.pPoolSizes = sizes;
    if (!VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &pool_)))
      return false;

    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &skybox_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &skybox_set_)))
      return false;
  }
  {
    const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                       kMaxTextures};
    VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    info.maxSets = 1;
    info.poolSizeCount = 1;
    info.pPoolSizes = &size;
    if (!VK_CHECK(
            vkCreateDescriptorPool(device, &info, nullptr, &texture_pool_))) {
      return false;
    }
    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = texture_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &texture_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &texture_set_)))
      return false;
  }
  {
    VkSamplerCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = info.addressModeV = info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.anisotropyEnable = context_->sampler_anisotropy ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = context_->sampler_anisotropy ? 8.0f : 1.0f;
    if (!VK_CHECK(vkCreateSampler(device, &info, nullptr, &material_sampler_)))
      return false;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy = 1.0f;
    info.addressModeU = info.addressModeV = info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!VK_CHECK(vkCreateSampler(device, &info, nullptr, &clamp_sampler_)))
      return false;
  }
  return true;
}

bool SceneRenderer::CreateFrameResources() {
  constexpr uint32_t kInitialDraws = 4096;
  if (!draw_buffer_.Create(context_->allocator,
                           sizeof(DrawData) * kInitialDraws,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           Buffer::Memory::kDeviceLocal) ||
      !instance_buffer_.Create(
          context_->allocator, sizeof(uint32_t) * kInitialDraws,
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Buffer::Memory::kHostVisible)) {
    return false;
  }
  {
    std::vector<uint32_t> iota(kInitialDraws);
    for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
    instance_buffer_.Write(iota.data(), iota.size() * sizeof(uint32_t));
  }
  frames_.resize(frames_in_flight_);
  for (auto& frame : frames_) {
    using Memory = Buffer::Memory;
    if (!frame.uniforms.Create(
            context_->allocator, uniform_stride_ * kMaxPasses,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Memory::kHostVisible) ||
        !frame.bones.Create(context_->allocator, sizeof(glm::mat4) * 4096,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            Memory::kHostVisible)) {
      return false;
    }
    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &frame_layout_;
    if (!VK_CHECK(
            vkAllocateDescriptorSets(context_->device, &alloc, &frame.set)))
      return false;
  }
  return true;
}

void SceneRenderer::UpdateFrameSet(FrameResources& frame) {
  VkDescriptorBufferInfo uniform = {frame.uniforms.Get(), 0,
                                    sizeof(FrameUniforms)};
  VkDescriptorBufferInfo draws = {draw_buffer_.Get(), 0, VK_WHOLE_SIZE};
  frame.bound_draws = draw_buffer_.Get();
  VkDescriptorBufferInfo bones = {frame.bones.Get(), 0, VK_WHOLE_SIZE};
  VkDescriptorImageInfo samplers[2] = {
      {material_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
      {clamp_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
  };
  VkWriteDescriptorSet writes[5] = {};
  for (int i = 0; i < 5; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = frame.set;
    writes[i].dstBinding = static_cast<uint32_t>(i);
    writes[i].descriptorCount = 1;
  }
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  writes[0].pBufferInfo = &uniform;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[1].pBufferInfo = &draws;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[2].pBufferInfo = &bones;
  writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  writes[3].pImageInfo = &samplers[0];
  writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  writes[4].pImageInfo = &samplers[1];
  vkUpdateDescriptorSets(context_->device, 5, writes, 0, nullptr);
}

VkPipeline SceneRenderer::BuildModelPipeline(VkFormat color_format,
                                             bool transparent,
                                             VkCullModeFlags cull) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = model_vs_;
  stages[0].pName = "Main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = model_ps_;
  stages[1].pName = "Main";

  const VkVertexInputBindingDescription bindings[] = {
      {0, sizeof(ModelVertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(uint32_t), VK_VERTEX_INPUT_RATE_INSTANCE},
  };
  const VkVertexInputAttributeDescription attributes[] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertex, normal)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ModelVertex, uv)},
      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, color)},
      {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(ModelVertex, joints)},
      {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, weights)},
      {6, 1, VK_FORMAT_R32_UINT, 0},
      {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertex, tangent)},
  };
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 2;
  vertex_input.pVertexBindingDescriptions = bindings;
  vertex_input.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(std::size(attributes));
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
  rasterizer.cullMode = cull;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = transparent ? VK_FALSE : VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPipelineColorBlendAttachmentState blend = {};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  if (transparent) {
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo color_blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend;

  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                           VK_DYNAMIC_STATE_SCISSOR,
                                           VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 3;
  dynamic_state.pDynamicStates = dynamic_states;

  VkPipelineRenderingCreateInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &color_format;
  rendering.depthAttachmentFormat = kDepthFormat;

  VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input_assembly;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &rasterizer;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth_stencil;
  info.pColorBlendState = &color_blend;
  info.pDynamicState = &dynamic_state;
  info.layout = model_layout_;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VK_CHECK(vkCreateGraphicsPipelines(context_->device, VK_NULL_HANDLE, 1, &info,
                                     nullptr, &pipeline));
  return pipeline;
}

VkPipeline SceneRenderer::BuildSkyboxPipeline(VkFormat color_format) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = skybox_vs_;
  stages[0].pName = "Main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = skybox_ps_;
  stages[1].pName = "Main";

  VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState blend = {};
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

  VkPipelineRenderingCreateInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &color_format;
  rendering.depthAttachmentFormat = kDepthFormat;

  VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input_assembly;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &rasterizer;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth_stencil;
  info.pColorBlendState = &color_blend;
  info.pDynamicState = &dynamic_state;
  info.layout = skybox_pipeline_layout_;

  VkPipeline pipeline = VK_NULL_HANDLE;
  VK_CHECK(vkCreateGraphicsPipelines(context_->device, VK_NULL_HANDLE, 1, &info,
                                     nullptr, &pipeline));
  return pipeline;
}

const SceneRenderer::FormatPipelines& SceneRenderer::GetPipelines(
    VkFormat color_format) {
  auto it = pipelines_.find(color_format);
  if (it != pipelines_.end()) return it->second;
  FormatPipelines p;
  p.opaque[0] = BuildModelPipeline(color_format, false, VK_CULL_MODE_BACK_BIT);
  p.opaque[1] = BuildModelPipeline(color_format, false, VK_CULL_MODE_NONE);
  p.transparent[0] =
      BuildModelPipeline(color_format, true, VK_CULL_MODE_BACK_BIT);
  p.transparent[1] = BuildModelPipeline(color_format, true, VK_CULL_MODE_NONE);
  p.skybox = BuildSkyboxPipeline(color_format);
  return pipelines_.emplace(color_format, p).first->second;
}

void SceneRenderer::Shutdown() {
  if (!context_) return;
  VkDevice device = context_->device;
  vkDeviceWaitIdle(device);

  depth_targets_.clear();
  for (auto& [path, rt] : render_textures_) rt.color.Reset();
  render_textures_.clear();
  textures_.clear();
  geometry_.clear();
  garbage_.clear();
  image_garbage_.clear();
  free_slots_.clear();
  frames_.clear();
  white_.Reset();
  vertex_buffer_.Reset();
  index_buffer_.Reset();
  draw_buffer_.Reset();
  instance_buffer_.Reset();

  if (hooks_) hooks_->renderer = nullptr;
  hooks_.reset();
  registry_ = nullptr;
  draws_.clear();
  records_.clear();

  for (auto& [fmt, fp] : pipelines_) {
    for (VkPipeline p :
         {fp.opaque[0], fp.opaque[1], fp.transparent[0], fp.transparent[1],
          fp.skybox}) {
      if (p) vkDestroyPipeline(device, p, nullptr);
    }
  }
  pipelines_.clear();
  for (VkShaderModule* m :
       {&model_vs_, &model_ps_, &skybox_vs_, &skybox_ps_}) {
    if (*m) vkDestroyShaderModule(device, *m, nullptr);
    *m = VK_NULL_HANDLE;
  }
  for (VkSampler* s : {&material_sampler_, &clamp_sampler_}) {
    if (*s) vkDestroySampler(device, *s, nullptr);
    *s = VK_NULL_HANDLE;
  }
  if (pool_) vkDestroyDescriptorPool(device, pool_, nullptr);
  if (texture_pool_) vkDestroyDescriptorPool(device, texture_pool_, nullptr);
  pool_ = texture_pool_ = VK_NULL_HANDLE;
  for (VkPipelineLayout* l : {&model_layout_, &skybox_pipeline_layout_}) {
    if (*l) vkDestroyPipelineLayout(device, *l, nullptr);
    *l = VK_NULL_HANDLE;
  }
  for (VkDescriptorSetLayout* l :
       {&frame_layout_, &texture_layout_, &skybox_layout_}) {
    if (*l) vkDestroyDescriptorSetLayout(device, *l, nullptr);
    *l = VK_NULL_HANDLE;
  }
  context_ = nullptr;
}

// ----------------------------------------------------------------- Uploads --

void SceneRenderer::BeginFrame(uint32_t frame_index) {
  frame_index_ = frame_index;
  frame_number_++;
  if (frames_.empty()) return;
  frames_[frame_index].staging.clear();
  frames_[frame_index].passes_used = 0;
  std::erase_if(garbage_, [&](const Garbage& g) {
    return g.frame + frames_in_flight_ + 1 <= frame_number_;
  });
}

bool SceneRenderer::GrowBuffer(VkCommandBuffer cmd, Buffer& buffer,
                               VkDeviceSize needed, VkBufferUsageFlags usage) {
  if (needed <= buffer.Size()) return true;
  Buffer bigger;
  const VkDeviceSize size = std::max(needed, buffer.Size() * 2);
  if (!bigger.Create(context_->allocator, size, usage,
                     Buffer::Memory::kDeviceLocal)) {
    PDebug::error("Vulkan: could not grow a model buffer to {} bytes", size);
    return false;
  }
  if (buffer.Valid()) {
    GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy region = {0, 0, buffer.Size()};
    vkCmdCopyBuffer(cmd, buffer.Get(), bigger.Get(), 1, &region);
    GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
    garbage_.push_back({frame_number_, std::move(buffer)});
  }
  buffer = std::move(bigger);
  return true;
}

const SceneRenderer::GpuGeometry* SceneRenderer::UploadGeometry(
    VkCommandBuffer cmd, FrameResources& frame,
    const std::shared_ptr<const MeshGeometry>& geometry) {
  if (!geometry || geometry->indices.empty()) return nullptr;
  if (auto it = geometry_.find(geometry->id); it != geometry_.end())
    return &it->second;

  const uint32_t vertex_count =
      static_cast<uint32_t>(geometry->vertices.size());
  const uint32_t index_count = static_cast<uint32_t>(geometry->indices.size());
  const VkBufferUsageFlags vertex_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  const VkBufferUsageFlags index_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (!GrowBuffer(
          cmd, vertex_buffer_,
          sizeof(ModelVertex) * (VkDeviceSize(vertices_used_) + vertex_count),
          vertex_usage) ||
      !GrowBuffer(
          cmd, index_buffer_,
          sizeof(uint32_t) * (VkDeviceSize(indices_used_) + index_count),
          index_usage)) {
    return nullptr;
  }

  const VkDeviceSize vertex_bytes = sizeof(ModelVertex) * vertex_count;
  const VkDeviceSize index_bytes = sizeof(uint32_t) * index_count;
  Buffer staging;
  if (!staging.Create(context_->allocator, vertex_bytes + index_bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kHostVisible) ||
      !staging.Write(geometry->vertices.data(), vertex_bytes) ||
      !staging.Write(geometry->indices.data(), index_bytes, vertex_bytes)) {
    return nullptr;
  }
  const VkBufferCopy vertex_copy = {
      0, sizeof(ModelVertex) * VkDeviceSize(vertices_used_), vertex_bytes};
  const VkBufferCopy index_copy = {
      vertex_bytes, sizeof(uint32_t) * VkDeviceSize(indices_used_),
      index_bytes};
  vkCmdCopyBuffer(cmd, staging.Get(), vertex_buffer_.Get(), 1, &vertex_copy);
  vkCmdCopyBuffer(cmd, staging.Get(), index_buffer_.Get(), 1, &index_copy);
  frame.staging.push_back(std::move(staging));

  GpuGeometry gpu;
  gpu.first_vertex = vertices_used_;
  gpu.first_index = indices_used_;
  gpu.index_count = index_count;
  gpu.source = geometry;
  vertices_used_ += vertex_count;
  indices_used_ += index_count;
  return &geometry_.emplace(geometry->id, gpu).first->second;
}

void SceneRenderer::WriteTextureSlot(uint32_t slot, VkImageView view) {
  VkDescriptorImageInfo image = {VK_NULL_HANDLE, view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write = {.sType =
                                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = texture_set_;
  write.dstBinding = 0;
  write.dstArrayElement = slot;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image;
  vkUpdateDescriptorSets(context_->device, 1, &write, 0, nullptr);
}

uint32_t SceneRenderer::TextureSlot(VkCommandBuffer cmd, FrameResources& frame,
                                    const std::shared_ptr<ImageData>& image) {
  if (!image || image->pixels.empty()) return 0;
  if (auto it = textures_.find(image->id); it != textures_.end())
    return it->second.slot;
  if (free_slots_.empty() && next_slot_ >= kMaxTextures) {
    PDebug::error("Vulkan: more than {} textures", kMaxTextures);
    return 0;
  }
  const uint32_t width = static_cast<uint32_t>(image->width);
  const uint32_t height = static_cast<uint32_t>(image->height);
  const uint32_t mips =
      static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
  GpuTexture texture;
  const VkFormat format =
      image->srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  if (!texture.image.Create(
          context_->allocator, context_->device, width, height, format,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT, mips)) {
    return 0;
  }
  Buffer staging;
  if (!staging.Create(context_->allocator, image->pixels.size(),
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kHostVisible) ||
      !staging.Write(image->pixels.data(), image->pixels.size())) {
    return 0;
  }
  VkImage vk_image = texture.image.Get();
  TransitionImage(
      cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
      VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, staging.Get(), vk_image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
  for (uint32_t level = 1; level < mips; level++) {
    TransitionImageMips(
        cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);
    const int32_t nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    VkImageBlit blit = {};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
    blit.srcOffsets[1] = {w, h, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    blit.dstOffsets[1] = {nw, nh, 1};
    vkCmdBlitImage(cmd, vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
    w = nw;
    h = nh;
  }
  if (mips > 1) {
    TransitionImageMips(
        cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, mips - 1,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  TransitionImageMips(
      cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  frame.staging.push_back(std::move(staging));
  if (!free_slots_.empty()) {
    texture.slot = free_slots_.back();
    free_slots_.pop_back();
  } else {
    texture.slot = next_slot_++;
  }
  texture.source = image;
  WriteTextureSlot(texture.slot, texture.image.View());
  const uint32_t slot = texture.slot;
  textures_.emplace(image->id, std::move(texture));
  return slot;
}

uint32_t SceneRenderer::RenderTextureSlot(const std::string& path, int width,
                                          int height) {
  auto it = render_textures_.find(path);
  if (it != render_textures_.end()) return it->second.slot;
  if (next_slot_ >= kMaxTextures) return 0;
  RenderTextureTarget rt;
  width = std::clamp(width, 1, 8192);
  height = std::clamp(height, 1, 8192);
  if (!rt.color.Create(
          context_->allocator, context_->device, static_cast<uint32_t>(width),
          static_cast<uint32_t>(height), kRenderTextureFormat,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT)) {
    return 0;
  }
  rt.slot = next_slot_++;
  rt.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  WriteTextureSlot(rt.slot, white_.View());
  const uint32_t slot = rt.slot;
  render_textures_.emplace(path, std::move(rt));
  return slot;
}

void SceneRenderer::ReleaseUnused() {
  std::erase_if(image_garbage_, [&](ImageGarbage& g) {
    if (g.frame + frames_in_flight_ + 1 > frame_number_) return false;
    WriteTextureSlot(g.slot, white_.View());
    free_slots_.push_back(g.slot);
    return true;
  });
  if (frame_number_ % 30 != 0) return;
  for (auto it = textures_.begin(); it != textures_.end();) {
    if (it->second.source.expired()) {
      image_garbage_.push_back(
          {frame_number_, std::move(it->second.image), it->second.slot});
      it = textures_.erase(it);
    } else {
      ++it;
    }
  }
  uint64_t live_vertices = 0;
  for (auto it = geometry_.begin(); it != geometry_.end();) {
    if (it->second.source.expired()) {
      it = geometry_.erase(it);
    } else {
      live_vertices += it->second.source.lock()->vertices.size();
      ++it;
    }
  }
  constexpr uint32_t kInitialVertices = 262144;
  constexpr uint32_t kInitialIndices = 1048576;
  if (vertices_used_ <= kInitialVertices || live_vertices * 2 > vertices_used_)
    return;
  Buffer vertices, indices;
  if (!vertices.Create(
          context_->allocator, sizeof(ModelVertex) * kInitialVertices,
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          Buffer::Memory::kDeviceLocal) ||
      !indices.Create(context_->allocator, sizeof(uint32_t) * kInitialIndices,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kDeviceLocal)) {
    return;
  }
  garbage_.push_back({frame_number_, std::move(vertex_buffer_)});
  garbage_.push_back({frame_number_, std::move(index_buffer_)});
  vertex_buffer_ = std::move(vertices);
  index_buffer_ = std::move(indices);
  vertex_capacity_ = kInitialVertices;
  index_capacity_ = kInitialIndices;
  vertices_used_ = 0;
  indices_used_ = 0;
  geometry_.clear();
  rebuild_all_ = true;
}

// ----------------------------------------------------------------- Prepare --

void SceneRenderer::Prepare(VkCommandBuffer cmd, GameScene& scene) {
  FrameResources& frame = frames_[frame_index_];
  bones_.clear();

  if (white_.Get() == VK_NULL_HANDLE) {
    white_.Create(context_->allocator, context_->device, 1, 1,
                  VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);
    Buffer staging;
    const uint32_t pixel = 0xffffffffu;
    staging.Create(context_->allocator, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   Buffer::Memory::kHostVisible);
    staging.Write(&pixel, 4);
    TransitionImage(
        cmd, white_.Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging.Get(), white_.Get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    TransitionImage(
        cmd, white_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    frame.staging.push_back(std::move(staging));
    WriteTextureSlot(0, white_.View());
  }

  ReleaseUnused();
  {
    MERCURY_PROFILE_SCOPE("sync_scene");
    SyncScene(cmd, frame, scene);
  }
  {
    uint64_t live = 0, triangles = 0;
    for (const auto& draw : draws_) {
      if (!draw.alive) continue;
      live++;
      triangles += draw.index_count / 3;
    }
    Profiler::SetCounter("draws", static_cast<double>(live));
    Profiler::SetCounter("triangles", static_cast<double>(triangles));
  }

  const VkDeviceSize bone_bytes =
      sizeof(glm::mat4) * std::max<size_t>(bones_.size(), 1);
  bool rebind = false;
  if (bone_bytes > frame.bones.Size()) {
    Buffer bigger;
    if (bigger.Create(
            context_->allocator, std::max(bone_bytes, frame.bones.Size() * 2),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Buffer::Memory::kHostVisible)) {
      frame.bones = std::move(bigger);
      rebind = true;
    }
  }
  if (!bones_.empty())
    frame.bones.Write(bones_.data(), sizeof(glm::mat4) * bones_.size());
  UploadRecords(cmd, frame);
  if (rebind || frame.bound_draws != draw_buffer_.Get()) UpdateFrameSet(frame);

  GlobalBarrier(cmd,
                VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                    VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);
}

// ------------------------------------------------------------ Scene slots --

void RenderHooks::OnMeshChanged(entt::registry& registry, entt::entity entity) {
  if (renderer && &registry == renderer->registry_)
    renderer->changed_.push_back(entity);
}

void RenderHooks::OnProxyDestroyed(entt::registry& registry,
                                   entt::entity entity) {
  if (renderer && &registry == renderer->registry_) {
    renderer->FreeProxySlots(registry.get<RenderProxy>(entity));
  }
}

void SceneRenderer::AttachRegistry(entt::registry& registry) {
  if (hooks_) hooks_->renderer = nullptr;
  FreeAllSlots();
  registry_ = &registry;
  if (auto* existing = registry.try_ctx<std::shared_ptr<RenderHooks>>()) {
    hooks_ = *existing;
  } else {
    hooks_ = std::make_shared<RenderHooks>();
    registry.set<std::shared_ptr<RenderHooks>>(hooks_);
    registry.on_construct<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_update<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_destroy<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_construct<InactiveTag>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_destroy<InactiveTag>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_update<NodeInfo>().connect<&RenderHooks::OnMeshChanged>(
        *hooks_);
    registry.on_destroy<RenderProxy>().connect<&RenderHooks::OnProxyDestroyed>(
        *hooks_);
  }
  registry.clear<RenderProxy>();
  hooks_->renderer = this;
  changed_.clear();
}

void SceneRenderer::FreeAllSlots() {
  draws_.clear();
  records_.clear();
  free_draw_slots_.clear();
  dirty_records_.clear();
  record_dirty_.clear();
}

uint32_t SceneRenderer::AllocateSlot() {
  if (!free_draw_slots_.empty()) {
    const uint32_t slot = free_draw_slots_.back();
    free_draw_slots_.pop_back();
    return slot;
  }
  const uint32_t slot = static_cast<uint32_t>(draws_.size());
  draws_.emplace_back();
  records_.emplace_back();
  record_dirty_.push_back(0);
  return slot;
}

void SceneRenderer::MarkSlotDirty(uint32_t slot) {
  if (slot < record_dirty_.size() && !record_dirty_[slot]) {
    record_dirty_[slot] = 1;
    dirty_records_.push_back(slot);
  }
}

void SceneRenderer::FreeProxySlots(RenderProxy& proxy) {
  for (uint32_t slot : proxy.slots) {
    if (slot >= draws_.size()) continue;
    draws_[slot] = FrameDraw{};
    free_draw_slots_.push_back(slot);
  }
  proxy.slots.clear();
}

void SceneRenderer::SyncScene(VkCommandBuffer cmd, FrameResources& frame,
                              GameScene& scene) {
  auto& registry = scene.GetRegistry();
  if (&registry != registry_) AttachRegistry(registry);

  for (entt::entity entity : changed_) {
    if (!registry.valid(entity)) continue;
    auto* proxy = registry.try_get<RenderProxy>(entity);
    if (!proxy) continue;
    if (!registry.all_of<RenderMesh>(entity)) {
      registry.remove<RenderProxy>(entity);
    } else {
      proxy->dirty = true;
    }
  }
  changed_.clear();
  if (rebuild_all_) {
    rebuild_all_ = false;
    for (auto [entity, proxy] : registry.view<RenderProxy>().each())
      proxy.dirty = true;
  }

  std::vector<entt::entity> fresh;
  for (auto entity :
       registry.view<RenderMesh, Transform>(entt::exclude<RenderProxy>)) {
    fresh.push_back(entity);
  }
  for (entt::entity entity : fresh) registry.emplace<RenderProxy>(entity);

  for (auto [entity, mesh, transform, proxy] :
       registry.view<RenderMesh, Transform, RenderProxy>().each()) {
    const glm::vec3 position = transform.GetInterpolatedPosition();
    const glm::vec3 rotation = transform.GetInterpolatedRotation();
    const glm::vec3 scale = transform.GetInterpolatedScale();
    const bool moved =
        proxy.built && (position != proxy.position ||
                        rotation != proxy.rotation || scale != proxy.scale);
    if (!proxy.dirty && !proxy.animated && !moved &&
        mesh.visible == proxy.mesh_visible) {
      continue;
    }
    if (proxy.dirty || !proxy.built) {
      proxy.inactive = registry.all_of<InactiveTag>(entity);
      proxy.layer = 0;
      if (auto* info = registry.try_get<NodeInfo>(entity)) {
        proxy.layer = static_cast<uint32_t>(info->layer);
      }
    }
    proxy.position = position;
    proxy.rotation = rotation;
    proxy.scale = scale;
    proxy.mesh_visible = mesh.visible;
    proxy.visible = mesh.visible && !proxy.inactive;
    BuildProxy(cmd, frame, scene, entity, mesh, transform, proxy);
  }
}

void SceneRenderer::BuildProxy(VkCommandBuffer cmd, FrameResources& frame,
                               GameScene& scene, entt::entity entity,
                               const RenderMesh& mesh,
                               const Transform& transform, RenderProxy& proxy) {
  auto& registry = scene.GetRegistry();
  proxy.built = true;
  proxy.dirty = false;
  const ModelPose* pose = nullptr;
  if (mesh.pose_entity != entt::null && registry.valid(mesh.pose_entity)) {
    pose = registry.try_get<ModelPose>(mesh.pose_entity);
  }
  bool skinned = false;
  for (const auto& draw : mesh.draws) skinned |= draw.skin >= 0;
  proxy.animated = pose != nullptr || skinned;

  std::vector<const RenderDraw*> drawn;
  if (proxy.visible) {
    for (const auto& draw : mesh.draws) {
      if (draw.geometry && !draw.geometry->indices.empty())
        drawn.push_back(&draw);
    }
  }
  while (proxy.slots.size() > drawn.size()) {
    const uint32_t slot = proxy.slots.back();
    proxy.slots.pop_back();
    draws_[slot] = FrameDraw{};
    free_draw_slots_.push_back(slot);
  }
  while (proxy.slots.size() < drawn.size())
    proxy.slots.push_back(AllocateSlot());
  if (drawn.empty()) return;

  auto* map_scene = dynamic_cast<MapScene*>(&scene);
  const glm::mat4 world =
      glm::translate(glm::mat4(1.0f), transform.GetInterpolatedPosition()) *
      transform.GetRotationMatrix() *
      glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());
  std::vector<glm::mat4> skin;
  for (size_t i = 0; i < drawn.size(); i++) {
    const RenderDraw& draw = *drawn[i];
    const uint32_t slot = proxy.slots[i];
    const GpuGeometry* geometry = UploadGeometry(cmd, frame, draw.geometry);
    FrameDraw& frame_draw = draws_[slot];
    if (!geometry) {
      frame_draw = FrameDraw{};
      continue;
    }
    static const RenderMaterial kDefaultMaterial;
    const RenderMaterial& material =
        mesh.materials.empty() ? kDefaultMaterial
                               : mesh.materials[std::min<size_t>(
                                     draw.material, mesh.materials.size() - 1)];
    DrawData& record = records_[slot];
    record = {};
    uint32_t bone_offset = kNoSkin;
    if (draw.skin >= 0) {
      mesh.SkinMatrices(draw, pose, skin);
      bone_offset = static_cast<uint32_t>(bones_.size());
      bones_.insert(bones_.end(), skin.begin(), skin.end());
      record.world = world;
    } else {
      record.world = world * mesh.DrawMatrix(draw, pose);
    }
    record.color =
        glm::vec4(SrgbToLinear(glm::vec3(material.color)), material.color.a);
    record.uv = glm::vec4(material.tiling, material.offset);
    uint32_t texture = 0;
    uint32_t flags = 0;
    if (!material.render_texture.empty()) {
      int width = 1024, height = 1024;
      if (map_scene)
        map_scene->GetRenderTextureSize(material.render_texture, width, height);
      texture = RenderTextureSlot(material.render_texture, width, height);
      flags |= kFlagClampTexture;
      record.uv.y = -record.uv.y;
      record.uv.w = 1.0f - record.uv.w;
    } else {
      texture = TextureSlot(cmd, frame, material.texture);
    }
    if (material.sdf_text) flags |= kFlagSdfText;
    record.info = glm::uvec4(texture, flags, bone_offset, proxy.layer);

    frame_draw = FrameDraw{};
    frame_draw.geometry = geometry;
    frame_draw.first_index = geometry->first_index;
    frame_draw.index_count = geometry->index_count;
    frame_draw.record = slot;
    frame_draw.layer = proxy.layer;
    frame_draw.alive = true;
    frame_draw.transparent = material.transparent || material.transmission > 0.0f;
    frame_draw.double_sided = material.double_sided || material.sdf_text;
    frame_draw.mirrored = glm::determinant(glm::mat3(record.world)) < 0.0f;
    const glm::vec3 local_center =
        (draw.geometry->bounds_min + draw.geometry->bounds_max) * 0.5f;
    frame_draw.center = glm::vec3(record.world * glm::vec4(local_center, 1.0f));
    MarkSlotDirty(slot);
  }
}

void SceneRenderer::UploadRecords(VkCommandBuffer cmd, FrameResources& frame) {
  const VkDeviceSize needed =
      sizeof(DrawData) * std::max<size_t>(records_.size(), 1);
  if (needed > draw_buffer_.Size()) {
    Buffer bigger;
    const VkDeviceSize size = std::max(needed, draw_buffer_.Size() * 2);
    if (!bigger.Create(context_->allocator, size,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       Buffer::Memory::kDeviceLocal)) {
      PDebug::error("Vulkan: could not grow the draw buffer to {} bytes", size);
      return;
    }
    garbage_.push_back({frame_number_, std::move(draw_buffer_)});
    draw_buffer_ = std::move(bigger);
    const VkDeviceSize count = draw_buffer_.Size() / sizeof(DrawData);
    Buffer instances;
    if (instances.Create(context_->allocator, count * sizeof(uint32_t),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         Buffer::Memory::kHostVisible)) {
      std::vector<uint32_t> iota(count);
      for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
      instances.Write(iota.data(), iota.size() * sizeof(uint32_t));
      garbage_.push_back({frame_number_, std::move(instance_buffer_)});
      instance_buffer_ = std::move(instances);
    }
    for (size_t slot = 0; slot < records_.size(); slot++) MarkSlotDirty(slot);
  }
  if (dirty_records_.empty()) return;

  std::sort(dirty_records_.begin(), dirty_records_.end());
  const size_t count = dirty_records_.size();
  Buffer staging;
  if (!staging.Create(context_->allocator, sizeof(DrawData) * count,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kHostVisible)) {
    return;
  }
  auto* mapped = static_cast<uint8_t*>(staging.Mapped());
  std::vector<VkBufferCopy> regions;
  for (size_t i = 0; i < count; i++) {
    const uint32_t slot = dirty_records_[i];
    std::memcpy(mapped + sizeof(DrawData) * i, &records_[slot],
                sizeof(DrawData));
    record_dirty_[slot] = 0;
    if (!regions.empty() && dirty_records_[i - 1] + 1 == slot) {
      regions.back().size += sizeof(DrawData);
    } else {
      regions.push_back(
          {sizeof(DrawData) * i, sizeof(DrawData) * slot, sizeof(DrawData)});
    }
  }
  staging.Flush(0, staging.Size());
  dirty_records_.clear();
  GlobalBarrier(cmd,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
  vkCmdCopyBuffer(cmd, staging.Get(), draw_buffer_.Get(),
                  static_cast<uint32_t>(regions.size()), regions.data());
  frame.staging.push_back(std::move(staging));
}

// ------------------------------------------------------------------ Render --

SceneRenderer::DepthTarget& SceneRenderer::GetDepthTarget(VkExtent2D extent) {
  for (auto& target : depth_targets_) {
    if (target->extent.width == extent.width &&
        target->extent.height == extent.height) {
      target->last_used = frame_number_;
      return *target;
    }
  }
  std::erase_if(depth_targets_, [&](const std::unique_ptr<DepthTarget>& t) {
    if (t->last_used + 8 < frame_number_) {
      vkDeviceWaitIdle(context_->device);
      return true;
    }
    return false;
  });
  auto target = std::make_unique<DepthTarget>();
  target->extent = extent;
  target->last_used = frame_number_;
  target->depth.Create(context_->allocator, context_->device, extent.width,
                       extent.height, kDepthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT);
  depth_targets_.push_back(std::move(target));
  return *depth_targets_.back();
}

uint32_t SceneRenderer::PushUniforms(FrameResources& frame,
                                     const FrameUniforms& uniforms) {
  const uint32_t index = frame.passes_used < kMaxPasses
                             ? frame.passes_used++
                             : kMaxPasses - 1;
  const uint32_t offset = static_cast<uint32_t>(index * uniform_stride_);
  frame.uniforms.Write(&uniforms, sizeof(uniforms), offset);
  return offset;
}

void SceneRenderer::DrawList(VkCommandBuffer cmd, FrameResources& frame,
                             uint32_t uniform_offset,
                             const std::vector<const FrameDraw*>& list,
                             const VkPipeline (&pipelines)[2]) {
  if (list.empty()) return;
  const VkDescriptorSet sets[2] = {frame.set, texture_set_};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_,
                          0, 2, sets, 1, &uniform_offset);
  const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
  const VkDeviceSize offsets[2] = {0, 0};
  vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
  vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
  VkPipeline bound = VK_NULL_HANDLE;
  FrontFace front_face;
  for (const FrameDraw* draw : list) {
    VkPipeline pipeline = pipelines[draw->double_sided ? 1 : 0];
    if (pipeline != bound) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      bound = pipeline;
    }
    front_face.Set(cmd, draw->mirrored);
    vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
                     static_cast<int32_t>(draw->geometry->first_vertex),
                     draw->record);
  }
}

void SceneRenderer::DrawCamera(VkCommandBuffer cmd, FrameResources& frame,
                               const RenderCamera& camera, VkImage output,
                               VkImageView output_view, VkExtent2D extent,
                               VkFormat output_format,
                               VkImageLayout output_layout, bool clear_color,
                               bool clear_depth, const LegacyDraws* legacy,
                               VkImageView sky_texture, VkSampler sky_sampler) {
  FrameUniforms uniforms = {};
  uniforms.view_proj = camera.projection * camera.view;
  uniforms.flags = glm::vec4(IsSrgbFormat(output_format) ? 0.0f : 1.0f, 0.0f,
                             0.0f, 0.0f);
  const uint32_t offset = PushUniforms(frame, uniforms);

  std::vector<const FrameDraw*> opaque, transparent;
  for (const auto& draw : draws_) {
    if (!draw.alive || !(camera.culling_mask & (1u << (draw.layer & 31))))
      continue;
    if (draw.transparent) {
      transparent.push_back(&draw);
    } else {
      opaque.push_back(&draw);
    }
  }

  DepthTarget& depth_target = GetDepthTarget(extent);
  if (output_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    TransitionImage(
        cmd, output, VK_IMAGE_ASPECT_COLOR_BIT, output_layout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kColorAccess,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess);
  }
  TransitionImage(cmd, depth_target.depth.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                  kDepthAccess, kDepthStages, kDepthAccess);

  const glm::vec4 bg = camera.background;
  VkRenderingAttachmentInfo color = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = output_view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp =
      clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.clearValue.color = {{bg.r, bg.g, bg.b, 1.0f}};

  VkRenderingAttachmentInfo depth = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = depth_target.depth.View();
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp =
      clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  rendering.pDepthAttachment = &depth;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, extent);

  const FormatPipelines& fp = GetPipelines(output_format);
  if (clear_color && camera.clear == static_cast<int>(CameraClear::kSkybox) &&
      sky_texture != VK_NULL_HANDLE && sky_sampler != VK_NULL_HANDLE) {
    if (last_sky_view_ != sky_texture || last_sky_sampler_ != sky_sampler) {
      last_sky_view_ = sky_texture;
      last_sky_sampler_ = sky_sampler;
      VkDescriptorImageInfo img = {VK_NULL_HANDLE, sky_texture,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkDescriptorImageInfo smp = {sky_sampler, VK_NULL_HANDLE,
                                   VK_IMAGE_LAYOUT_UNDEFINED};
      VkWriteDescriptorSet writes[2] = {};
      writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[0].dstSet = skybox_set_;
      writes[0].dstBinding = 10;
      writes[0].descriptorCount = 1;
      writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      writes[0].pImageInfo = &img;
      writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[1].dstSet = skybox_set_;
      writes[1].dstBinding = 20;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
      writes[1].pImageInfo = &smp;
      vkUpdateDescriptorSets(context_->device, 2, writes, 0, nullptr);
    }
    const glm::mat4 inv_sky =
        glm::inverse(camera.projection * glm::mat4(glm::mat3(camera.view)));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fp.skybox);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            skybox_pipeline_layout_, 0, 1, &skybox_set_, 0,
                            nullptr);
    vkCmdPushConstants(cmd, skybox_pipeline_layout_,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(inv_sky),
                       &inv_sky);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  if (legacy && *legacy) (*legacy)(cmd, extent);

  std::stable_sort(opaque.begin(), opaque.end(),
                   [](const FrameDraw* a, const FrameDraw* b) {
                     return a->double_sided < b->double_sided;
                   });
  DrawList(cmd, frame, offset, opaque, fp.opaque);

  std::sort(transparent.begin(), transparent.end(),
            [&](const FrameDraw* a, const FrameDraw* b) {
              const glm::vec3 da = a->center - camera.position,
                              db = b->center - camera.position;
              return glm::dot(da, da) > glm::dot(db, db);
            });
  DrawList(cmd, frame, offset, transparent, fp.transparent);

  vkCmdEndRendering(cmd);
}

void SceneRenderer::RenderView(VkCommandBuffer cmd, GameScene& scene,
                               const std::vector<RenderCamera>& cameras,
                               VkImage output, VkImageView output_view,
                               VkExtent2D extent, VkFormat output_format,
                               VkImageLayout output_layout,
                               const LegacyDraws& legacy,
                               VkImageView sky_texture, VkSampler sky_sampler) {
  (void)scene;
  FrameResources& frame = frames_[frame_index_];
  const RenderCamera* main = nullptr;
  std::vector<const RenderCamera*> overlays;
  for (const auto& camera : cameras) {
    if (!camera.target_texture.empty()) {
      RenderTextureSlot(camera.target_texture, camera.target_width,
                        camera.target_height);
      auto it = render_textures_.find(camera.target_texture);
      if (it == render_textures_.end()) continue;
      auto& rt = it->second;
      DrawCamera(cmd, frame, camera, rt.color.Get(), rt.color.View(),
                 rt.extent, kRenderTextureFormat,
                 rt.drawn ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_UNDEFINED,
                 true, true, nullptr, sky_texture, sky_sampler);
      TransitionImage(cmd, rt.color.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      kColorAccess, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      if (!rt.drawn) {
        rt.drawn = true;
        WriteTextureSlot(rt.slot, rt.color.View());
      }
    } else if (camera.overlay) {
      overlays.push_back(&camera);
    } else if (!main) {
      main = &camera;
    }
  }
  if (!main) return;

  {
    GpuScope scope(gpu_timer_, cmd, "main");
    const bool clear_color =
        main->clear != static_cast<int>(CameraClear::kNothing) &&
        main->clear != static_cast<int>(CameraClear::kDepthOnly);
    const bool clear_depth =
        main->clear != static_cast<int>(CameraClear::kNothing);
    DrawCamera(cmd, frame, *main, output, output_view, extent, output_format,
               output_layout, clear_color, clear_depth, &legacy, sky_texture,
               sky_sampler);
  }

  for (const RenderCamera* overlay : overlays) {
    const bool clear_color =
        overlay->clear == static_cast<int>(CameraClear::kColor) ||
        overlay->clear == static_cast<int>(CameraClear::kSkybox);
    DrawCamera(cmd, frame, *overlay, output, output_view, extent, output_format,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, clear_color, true,
               nullptr, sky_texture, sky_sampler);
  }
}

}  // namespace Mercury::vk
