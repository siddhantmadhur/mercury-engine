#include "vk_scene_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "components/components.h"
#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "properties/transform.h"
#include "scene/map_scene.h"
#include "vk_check.h"

namespace Mercury::vk {

namespace {

// Tracks the dynamic front face while recording draws (dynamic state starts
// undefined, so the first draw always sets it).
struct FrontFace {
  int current = -1;
  void Set(VkCommandBuffer cmd, bool mirrored) {
    if (current == static_cast<int>(mirrored)) return;
    current = mirrored;
    vkCmdSetFrontFace(cmd, mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
  }
};

// Uniform slots per frame: cameras, the sun, 6 per shadow cube and per probe
// captured.
constexpr uint32_t kMaxPasses = 512;
constexpr VkFormat kShadowCubeFormat = VK_FORMAT_R32_SFLOAT;
constexpr VkFormat kProbeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kProbeVisibilityFormat = VK_FORMAT_R32G32_SFLOAT;
constexpr VkFormat kLdrFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;
constexpr VkFormat kRenderTextureFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr uint32_t kNoSkin = 0xffffffffu;

// Must match kFaceForward / kFaceUp in lighting_math.hlsli.
constexpr glm::vec3 kFaceForward[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                       {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
constexpr glm::vec3 kFaceUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1},
                                  {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

// FNV-1a over floats rounded to 1/4096, to notice when lights or objects
// change. Rounding ignores the last-bit wobble of interpolated transforms.
void HashFloats(uint64_t& hash, const float* values, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const int64_t q = std::llround(static_cast<double>(values[i]) * 4096.0);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&q);
    for (size_t b = 0; b < sizeof(q); b++) {
      hash ^= bytes[b];
      hash *= 1099511628211ull;
    }
  }
}

// Whether a box (world space) may show through cube face `face` seen from
// `eye`, no farther than `far_clip`: a bounding sphere against the face's
// 90 degree frustum.
bool FaceSees(glm::vec3 eye, uint32_t face, glm::vec3 box_min, glm::vec3 box_max,
              float far_clip) {
  const glm::vec3 c = (box_min + box_max) * 0.5f - eye;
  const float r = glm::length(box_max - box_min) * 0.5f;
  const float f = glm::dot(c, kFaceForward[face]);
  if (f + r <= 0.0f || f - r > far_clip) return false;
  const glm::vec3 right = glm::cross(kFaceForward[face], kFaceUp[face]);
  const float k = r * 1.41421356f;
  return std::abs(glm::dot(c, right)) - f < k && std::abs(glm::dot(c, kFaceUp[face])) - f < k;
}

// Whether a box comes within `radius` of `point`.
bool BoxNear(glm::vec3 box_min, glm::vec3 box_max, glm::vec3 point, float radius) {
  const glm::vec3 d = glm::max(glm::max(box_min - point, point - box_max), glm::vec3(0.0f));
  return glm::dot(d, d) <= radius * radius;
}

constexpr uint64_t kHashSeed = 1469598103934665603ull;

constexpr uint32_t kFlagUnlit = 1;
constexpr uint32_t kFlagSdfText = 2;
constexpr uint32_t kFlagClampTexture = 4;
constexpr uint32_t kFlagTransparent = 8;

constexpr VkPipelineStageFlags2 kDepthStages =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
constexpr VkAccessFlags2 kColorAccess =
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr VkAccessFlags2 kDepthAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

float SrgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
glm::vec3 SrgbToLinear(glm::vec3 c) {
  return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)};
}

// Moves a color image to a color attachment, or back to shader reads.
void ToAttachment(VkCommandBuffer cmd, VkImage image, VkImageLayout from) {
  TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, from,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kColorAccess,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess);
}
void ToShaderRead(VkCommandBuffer cmd, VkImage image) {
  TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
void DepthToAttachment(VkCommandBuffer cmd, VkImage image) {
  TransitionImage(cmd, image, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | kDepthStages,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kDepthAccess, kDepthStages,
                  kDepthAccess);
}
void DepthToShaderRead(VkCommandBuffer cmd, VkImage image) {
  TransitionImage(cmd, image, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kDepthStages, kDepthAccess,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void SetViewport(VkCommandBuffer cmd, VkExtent2D extent) {
  // Negative height flips Y so clip space matches D3D (+Y up).
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

struct PostPush {
  glm::vec4 p[8] = {};
};
static_assert(sizeof(PostPush) == 128);

}  // namespace

void SceneRenderer::MainPassFormats(VkFormat (&colors)[2], VkFormat& depth) {
  colors[0] = kHdrFormat;
  colors[1] = kNormalFormat;
  depth = kDepthFormat;
}

// -------------------------------------------------------------------- Init --

bool SceneRenderer::Init(Context& context, uint32_t frames_in_flight,
                         const ShaderLoader& shaders) {
  context_ = &context;
  frames_in_flight_ = frames_in_flight;
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(context.physical_device, &properties);
  const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
  uniform_stride_ = (sizeof(FrameUniforms) + alignment - 1) / alignment * alignment;

  if (!CreateLayouts() || !CreatePipelines(shaders) || !CreateFrameResources() ||
      !CreateShadowMap() || !CreateLightingResources()) {
    PDebug::error("Vulkan: could not create the model renderer");
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
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(std::size(bindings));
    info.pBindings = bindings;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &frame_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                  kMaxTextures, VK_SHADER_STAGE_FRAGMENT_BIT,
                                                  nullptr};
    const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                           VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flags_info.bindingCount = 1;
    flags_info.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.pNext = &flags_info;
    info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    info.bindingCount = 1;
    info.pBindings = &binding;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &texture_layout_))) {
      return false;
    }
  }
  {
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    for (uint32_t i = 0; i < 6; i++) {
      bindings.push_back({2 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                          VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    }
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &post_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorSetLayout layouts[] = {frame_layout_, texture_layout_};
    VkPipelineLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 2;
    info.pSetLayouts = layouts;
    if (!VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &model_layout_))) {
      return false;
    }
  }
  {
    const VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
    VkPipelineLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = 1;
    info.pSetLayouts = &post_layout_;
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    if (!VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &post_pipeline_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frames_in_flight_},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames_in_flight_ * 2},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2048},
    };
    VkDescriptorPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = 1024;
    info.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    info.pPoolSizes = sizes;
    if (!VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &pool_))) return false;
  }
  {
    const VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxTextures};
    VkDescriptorPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    info.maxSets = 1;
    info.poolSizeCount = 1;
    info.pPoolSizes = &size;
    if (!VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &texture_pool_))) {
      return false;
    }
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = texture_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &texture_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &texture_set_))) return false;
  }
  {
    VkSamplerCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = VK_LOD_CLAMP_NONE;
    info.anisotropyEnable = context_->sampler_anisotropy ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = context_->sampler_anisotropy ? 8.0f : 1.0f;
    if (!VK_CHECK(vkCreateSampler(device, &info, nullptr, &material_sampler_))) return false;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy = 1.0f;
    info.addressModeU = info.addressModeV = info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!VK_CHECK(vkCreateSampler(device, &info, nullptr, &clamp_sampler_))) return false;
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (!VK_CHECK(vkCreateSampler(device, &info, nullptr, &point_sampler_))) return false;
    VkSamplerCreateInfo shadow = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    shadow.magFilter = VK_FILTER_LINEAR;
    shadow.minFilter = VK_FILTER_LINEAR;
    shadow.addressModeU = shadow.addressModeV = shadow.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadow.compareEnable = VK_TRUE;
    shadow.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadow.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (!VK_CHECK(vkCreateSampler(device, &shadow, nullptr, &shadow_sampler_))) return false;
  }
  return true;
}

VkPipeline SceneRenderer::BuildPipeline(VkShaderModule vs, VkShaderModule ps,
                                        bool model_input, const std::vector<VkFormat>& colors,
                                        VkFormat depth, int blend, bool depth_test,
                                        bool depth_write, VkCompareOp compare,
                                        VkCullModeFlags cull, bool depth_bias,
                                        VkPipelineLayout layout, uint32_t color_write_count) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs;
  stages[0].pName = "Main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = ps;
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
  if (model_input) {
    vertex_input.vertexBindingDescriptionCount = 2;
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributes));
    vertex_input.pVertexAttributeDescriptions = attributes;
  }
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
  if (depth_bias) {
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
  }
  VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_stencil.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
  depth_stencil.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
  depth_stencil.depthCompareOp = compare;

  // blend: 0 none, 1 alpha, 2 additive. Attachments past color_write_count
  // (when it is set) keep their contents.
  std::vector<VkPipelineColorBlendAttachmentState> blends(colors.size());
  for (size_t i = 0; i < colors.size(); i++) {
    auto& attachment = blends[i];
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (color_write_count > 0 && i >= color_write_count) attachment.colorWriteMask = 0;
    if (i == 0 && blend == 1) {
      attachment.blendEnable = VK_TRUE;
      attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      attachment.colorBlendOp = VK_BLEND_OP_ADD;
      attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (i == 0 && blend == 2) {
      attachment.blendEnable = VK_TRUE;
      attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      attachment.colorBlendOp = VK_BLEND_OP_ADD;
      attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
  }
  VkPipelineColorBlendStateCreateInfo color_blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = static_cast<uint32_t>(blends.size());
  color_blend.pAttachments = blends.data();
  // Front face is set per draw: negatively scaled objects wind the other way.
  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                           VK_DYNAMIC_STATE_FRONT_FACE};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 3;
  dynamic_state.pDynamicStates = dynamic_states;
  VkPipelineRenderingCreateInfo rendering = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
  rendering.pColorAttachmentFormats = colors.data();
  rendering.depthAttachmentFormat = depth;

  VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = ps ? 2 : 1;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &input_assembly;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &rasterizer;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth_stencil;
  info.pColorBlendState = colors.empty() ? nullptr : &color_blend;
  info.pDynamicState = &dynamic_state;
  info.layout = layout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (!VK_CHECK(vkCreateGraphicsPipelines(context_->device, VK_NULL_HANDLE, 1, &info,
                                          nullptr, &pipeline))) {
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

bool SceneRenderer::CreatePipelines(const ShaderLoader& shaders) {
  VkDevice device = context_->device;
  VkShaderModule model_vs = shaders("model.vs.spv");
  VkShaderModule model_ps = shaders("model.ps.spv");
  VkShaderModule shadow_vs = shaders("model_shadow.vs.spv");
  VkShaderModule outline_ps = shaders("outline_mask.ps.spv");
  fullscreen_vs_ = shaders("fullscreen.vs.spv");
  VkShaderModule sky_ps = shaders("sky.ps.spv");
  VkShaderModule ssao_ps = shaders("ssao.ps.spv");
  VkShaderModule blur_ps = shaders("blur.ps.spv");
  VkShaderModule down_ps = shaders("bloom_down.ps.spv");
  VkShaderModule up_ps = shaders("bloom_up.ps.spv");
  composite_ps_ = shaders("composite.ps.spv");
  fxaa_ps_ = shaders("fxaa.ps.spv");
  VkShaderModule shadow_cube_ps = shaders("shadow_cube.ps.spv");
  VkShaderModule probe_sh_ps = shaders("probe_sh.ps.spv");
  VkShaderModule probe_visibility_ps = shaders("probe_visibility.ps.spv");
  auto destroy = [&] {
    for (VkShaderModule m : {model_vs, model_ps, shadow_vs, outline_ps, sky_ps, ssao_ps,
                             blur_ps, down_ps, up_ps, shadow_cube_ps, probe_sh_ps,
                             probe_visibility_ps}) {
      if (m) vkDestroyShaderModule(device, m, nullptr);
    }
  };
  if (!model_vs || !model_ps || !shadow_vs || !outline_ps || !fullscreen_vs_ || !sky_ps ||
      !ssao_ps || !blur_ps || !down_ps || !up_ps || !composite_ps_ || !fxaa_ps_ ||
      !shadow_cube_ps || !probe_sh_ps || !probe_visibility_ps) {
    destroy();
    return false;
  }
  const std::vector<VkFormat> main_colors = {kHdrFormat, kNormalFormat};
  const VkCullModeFlags culls[2] = {VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_NONE};
  bool ok = true;
  for (int i = 0; i < 2; i++) {
    opaque_[i] = BuildPipeline(model_vs, model_ps, true, main_colors, kDepthFormat, 0, true,
                               true, VK_COMPARE_OP_LESS_OR_EQUAL, culls[i], false, model_layout_);
    transparent_[i] =
        BuildPipeline(model_vs, model_ps, true, main_colors, kDepthFormat, 1, true, false,
                      VK_COMPARE_OP_LESS_OR_EQUAL, culls[i], false, model_layout_, 1);
    ok = ok && opaque_[i] && transparent_[i];
  }
  shadow_ = BuildPipeline(shadow_vs, VK_NULL_HANDLE, true, {}, kDepthFormat, 0, true, true,
                          VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE, true, model_layout_);
  outline_mask_ = BuildPipeline(model_vs, outline_ps, true, {kMaskFormat}, VK_FORMAT_UNDEFINED,
                                0, false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                                model_layout_);
  sky_ = BuildPipeline(fullscreen_vs_, sky_ps, false, main_colors, kDepthFormat, 0, false, false,
                       VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false, post_pipeline_layout_, 1);
  ssao_ = BuildPipeline(fullscreen_vs_, ssao_ps, false, {kMaskFormat}, VK_FORMAT_UNDEFINED, 0,
                        false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                        post_pipeline_layout_);
  blur_ = BuildPipeline(fullscreen_vs_, blur_ps, false, {kMaskFormat}, VK_FORMAT_UNDEFINED, 0,
                        false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                        post_pipeline_layout_);
  bloom_down_ = BuildPipeline(fullscreen_vs_, down_ps, false, {kHdrFormat}, VK_FORMAT_UNDEFINED,
                              0, false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                              post_pipeline_layout_);
  bloom_up_ = BuildPipeline(fullscreen_vs_, up_ps, false, {kHdrFormat}, VK_FORMAT_UNDEFINED, 2,
                            false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                            post_pipeline_layout_);
  shadow_cube_ = BuildPipeline(model_vs, shadow_cube_ps, true, {kShadowCubeFormat}, kDepthFormat,
                               0, true, true, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE,
                               false, model_layout_);
  // Probes see both faces of everything, to tell when they are inside.
  probe_capture_ = BuildPipeline(model_vs, model_ps, true, main_colors, kDepthFormat, 0, true,
                                 true, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE, false,
                                 model_layout_);
  probe_sh_ = BuildPipeline(fullscreen_vs_, probe_sh_ps, false, {kProbeFormat},
                            VK_FORMAT_UNDEFINED, 0, false, false, VK_COMPARE_OP_ALWAYS,
                            VK_CULL_MODE_NONE, false, post_pipeline_layout_);
  probe_visibility_pass_ = BuildPipeline(fullscreen_vs_, probe_visibility_ps, false,
                                         {kProbeVisibilityFormat}, VK_FORMAT_UNDEFINED, 0, false,
                                         false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, false,
                                         post_pipeline_layout_);
  ok = ok && shadow_ && outline_mask_ && sky_ && ssao_ && blur_ && bloom_down_ && bloom_up_ &&
       shadow_cube_ && probe_capture_ && probe_sh_ && probe_visibility_pass_;
  destroy();
  return ok;
}

VkPipeline SceneRenderer::PipelineForFormat(VkFormat format, bool fxaa) {
  auto key = std::make_pair(format, fxaa);
  if (auto it = output_pipelines_.find(key); it != output_pipelines_.end()) return it->second;
  VkPipeline pipeline =
      BuildPipeline(fullscreen_vs_, fxaa ? fxaa_ps_ : composite_ps_, false, {format},
                    VK_FORMAT_UNDEFINED, 0, false, false, VK_COMPARE_OP_ALWAYS,
                    VK_CULL_MODE_NONE, false, post_pipeline_layout_);
  output_pipelines_[key] = pipeline;
  return pipeline;
}

bool SceneRenderer::CreateFrameResources() {
  frames_.resize(frames_in_flight_);
  for (auto& frame : frames_) {
    using Memory = Buffer::Memory;
    if (!frame.uniforms.Create(context_->allocator, uniform_stride_ * kMaxPasses,
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Memory::kHostVisible) ||
        !frame.draws.Create(context_->allocator, sizeof(DrawData) * 4096,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Memory::kHostVisible) ||
        !frame.bones.Create(context_->allocator, sizeof(glm::mat4) * 4096,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Memory::kHostVisible) ||
        !frame.instances.Create(context_->allocator, sizeof(uint32_t) * 4096,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Memory::kHostVisible)) {
      return false;
    }
    std::vector<uint32_t> iota(4096);
    for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
    frame.instances.Write(iota.data(), iota.size() * sizeof(uint32_t));
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &frame_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(context_->device, &alloc, &frame.set))) return false;
  }
  return true;
}

void SceneRenderer::UpdateFrameSet(FrameResources& frame) {
  VkDescriptorBufferInfo uniform = {frame.uniforms.Get(), 0, sizeof(FrameUniforms)};
  VkDescriptorBufferInfo draws = {frame.draws.Get(), 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo bones = {frame.bones.Get(), 0, VK_WHOLE_SIZE};
  VkDescriptorImageInfo shadow = {VK_NULL_HANDLE, shadow_image_.View(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo samplers[3] = {
      {shadow_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
      {material_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
      {clamp_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
  };
  VkDescriptorImageInfo cubes = {VK_NULL_HANDLE, shadow_cubes_.View(),
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo atlas = {VK_NULL_HANDLE, probe_atlas_.View(),
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo far_shadow = {VK_NULL_HANDLE, far_shadow_image_.View(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo visibility = {VK_NULL_HANDLE, probe_visibility_.View(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet writes[11] = {};
  for (int i = 0; i < 11; i++) {
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
  writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[3].pImageInfo = &shadow;
  for (int i = 0; i < 3; i++) {
    writes[4 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[4 + i].pImageInfo = &samplers[i];
  }
  writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[7].pImageInfo = &cubes;
  writes[8].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[8].pImageInfo = &atlas;
  writes[9].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[9].pImageInfo = &far_shadow;
  writes[10].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[10].pImageInfo = &visibility;
  vkUpdateDescriptorSets(context_->device, 11, writes, 0, nullptr);
}

bool SceneRenderer::CreateShadowMap() {
  if (!shadow_image_.Create(context_->allocator, context_->device, kShadowResolution,
                            kShadowResolution, kDepthFormat,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT)) {
    return false;
  }
  return true;
}

bool SceneRenderer::CreateLightingResources() {
  VmaAllocator a = context_->allocator;
  VkDevice d = context_->device;
  const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!shadow_cubes_.CreateArray(a, d, kShadowCubeResolution, kShadowCubeResolution,
                                 kMaxShadowCubes * 6, kShadowCubeFormat, color,
                                 VK_IMAGE_ASPECT_COLOR_BIT) ||
      !shadow_cube_depth_.Create(a, d, kShadowCubeResolution, kShadowCubeResolution,
                                 kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT) ||
      !probe_atlas_.Create(a, d, kProbesPerAtlasRow * 9, kMaxProbes / kProbesPerAtlasRow,
                           kProbeFormat, color | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_faces_.CreateArray(a, d, kProbeFaceSize, kProbeFaceSize, kMaxProbeBatch * 6,
                                kHdrFormat, color, VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_normal_.Create(a, d, kProbeFaceSize, kProbeFaceSize, kNormalFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_depth_.Create(a, d, kProbeFaceSize, kProbeFaceSize, kDepthFormat,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT) ||
      !probe_visibility_.Create(a, d, kProbesPerAtlasRow * kProbeVisibilitySize,
                                kMaxProbes / kProbesPerAtlasRow * kProbeVisibilitySize,
                                kProbeVisibilityFormat, color, VK_IMAGE_ASPECT_COLOR_BIT) ||
      !far_shadow_image_.Create(a, d, kFarShadowResolution, kFarShadowResolution, kDepthFormat,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT)) {
    return false;
  }
  for (auto& frame : frames_) UpdateFrameSet(frame);
  return true;
}

void SceneRenderer::Shutdown() {
  if (!context_) return;
  VkDevice device = context_->device;
  vkDeviceWaitIdle(device);
  for (auto& targets : targets_) DestroyTargets(*targets);
  targets_.clear();
  for (auto& [path, rt] : render_textures_) {
    if (rt.targets) DestroyTargets(*rt.targets);
    rt.color.Reset();
  }
  render_textures_.clear();
  textures_.clear();
  geometry_.clear();
  garbage_.clear();
  image_garbage_.clear();
  free_slots_.clear();
  frames_.clear();
  white_.Reset();
  shadow_image_.Reset();
  shadow_cubes_.Reset();
  far_shadow_image_.Reset();
  probe_visibility_.Reset();
  shadow_cube_depth_.Reset();
  probe_atlas_.Reset();
  probe_faces_.Reset();
  probe_normal_.Reset();
  probe_depth_.Reset();
  vertex_buffer_.Reset();
  index_buffer_.Reset();
  auto destroy_pipeline = [&](VkPipeline& p) {
    if (p) vkDestroyPipeline(device, p, nullptr);
    p = VK_NULL_HANDLE;
  };
  for (int i = 0; i < 2; i++) {
    destroy_pipeline(opaque_[i]);
    destroy_pipeline(transparent_[i]);
  }
  for (VkPipeline* p : {&shadow_, &outline_mask_, &sky_, &ssao_, &blur_, &bloom_down_,
                        &bloom_up_, &shadow_cube_, &probe_capture_, &probe_sh_,
                        &probe_visibility_pass_}) {
    destroy_pipeline(*p);
  }
  for (auto& [key, pipeline] : output_pipelines_) vkDestroyPipeline(device, pipeline, nullptr);
  output_pipelines_.clear();
  for (VkShaderModule* m : {&fullscreen_vs_, &composite_ps_, &fxaa_ps_}) {
    if (*m) vkDestroyShaderModule(device, *m, nullptr);
    *m = VK_NULL_HANDLE;
  }
  for (VkSampler* s : {&material_sampler_, &clamp_sampler_, &point_sampler_, &shadow_sampler_}) {
    if (*s) vkDestroySampler(device, *s, nullptr);
    *s = VK_NULL_HANDLE;
  }
  if (pool_) vkDestroyDescriptorPool(device, pool_, nullptr);
  if (texture_pool_) vkDestroyDescriptorPool(device, texture_pool_, nullptr);
  pool_ = texture_pool_ = VK_NULL_HANDLE;
  for (VkPipelineLayout* l : {&model_layout_, &post_pipeline_layout_}) {
    if (*l) vkDestroyPipelineLayout(device, *l, nullptr);
    *l = VK_NULL_HANDLE;
  }
  for (VkDescriptorSetLayout* l : {&frame_layout_, &texture_layout_, &post_layout_}) {
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

bool SceneRenderer::GrowBuffer(VkCommandBuffer cmd, Buffer& buffer, VkDeviceSize needed,
                               VkBufferUsageFlags usage) {
  if (needed <= buffer.Size()) return true;
  Buffer bigger;
  const VkDeviceSize size = std::max(needed, buffer.Size() * 2);
  if (!bigger.Create(context_->allocator, size, usage, Buffer::Memory::kDeviceLocal)) {
    PDebug::error("Vulkan: could not grow a model buffer to {} bytes", size);
    return false;
  }
  if (buffer.Valid()) {
    GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferCopy region = {0, 0, buffer.Size()};
    vkCmdCopyBuffer(cmd, buffer.Get(), bigger.Get(), 1, &region);
    GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    garbage_.push_back({frame_number_, std::move(buffer)});
  }
  buffer = std::move(bigger);
  return true;
}

const SceneRenderer::GpuGeometry* SceneRenderer::UploadGeometry(
    VkCommandBuffer cmd, FrameResources& frame,
    const std::shared_ptr<const MeshGeometry>& geometry) {
  if (!geometry || geometry->indices.empty()) return nullptr;
  if (auto it = geometry_.find(geometry->id); it != geometry_.end()) return &it->second;

  const uint32_t vertex_count = static_cast<uint32_t>(geometry->vertices.size());
  const uint32_t index_count = static_cast<uint32_t>(geometry->indices.size());
  const VkBufferUsageFlags vertex_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  const VkBufferUsageFlags index_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (!GrowBuffer(cmd, vertex_buffer_,
                  sizeof(ModelVertex) * (VkDeviceSize(vertices_used_) + vertex_count),
                  vertex_usage) ||
      !GrowBuffer(cmd, index_buffer_,
                  sizeof(uint32_t) * (VkDeviceSize(indices_used_) + index_count),
                  index_usage)) {
    return nullptr;
  }

  const VkDeviceSize vertex_bytes = sizeof(ModelVertex) * vertex_count;
  const VkDeviceSize index_bytes = sizeof(uint32_t) * index_count;
  Buffer staging;
  if (!staging.Create(context_->allocator, vertex_bytes + index_bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Buffer::Memory::kHostVisible) ||
      !staging.Write(geometry->vertices.data(), vertex_bytes) ||
      !staging.Write(geometry->indices.data(), index_bytes, vertex_bytes)) {
    return nullptr;
  }
  const VkBufferCopy vertex_copy = {0, sizeof(ModelVertex) * VkDeviceSize(vertices_used_),
                                    vertex_bytes};
  const VkBufferCopy index_copy = {vertex_bytes, sizeof(uint32_t) * VkDeviceSize(indices_used_),
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
  VkDescriptorImageInfo image = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
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
  if (auto it = textures_.find(image->id); it != textures_.end()) return it->second.slot;
  if (free_slots_.empty() && next_slot_ >= kMaxTextures) {
    PDebug::error("Vulkan: more than {} textures", kMaxTextures);
    return 0;
  }
  const uint32_t width = static_cast<uint32_t>(image->width);
  const uint32_t height = static_cast<uint32_t>(image->height);
  const uint32_t mips =
      static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
  GpuTexture texture;
  const VkFormat format = image->srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  if (!texture.image.Create(context_->allocator, context_->device, width, height, format,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT, mips)) {
    return 0;
  }
  Buffer staging;
  if (!staging.Create(context_->allocator, image->pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kHostVisible) ||
      !staging.Write(image->pixels.data(), image->pixels.size())) {
    return 0;
  }
  VkImage vk_image = texture.image.Get();
  TransitionImage(cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                  VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, staging.Get(), vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &region);
  // Mip chain by repeated halving blits.
  int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
  for (uint32_t level = 1; level < mips; level++) {
    TransitionImageMips(cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1,
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
    vkCmdBlitImage(cmd, vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    w = nw;
    h = nh;
  }
  if (mips > 1) {
    TransitionImageMips(cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, 0, mips - 1,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  TransitionImageMips(cmd, vk_image, VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1,
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

uint32_t SceneRenderer::RenderTextureSlot(const std::string& path, int width, int height) {
  auto it = render_textures_.find(path);
  if (it != render_textures_.end()) return it->second.slot;
  if (next_slot_ >= kMaxTextures) return 0;
  RenderTextureTarget rt;
  width = std::clamp(width, 1, 8192);
  height = std::clamp(height, 1, 8192);
  if (!rt.color.Create(context_->allocator, context_->device, static_cast<uint32_t>(width),
                       static_cast<uint32_t>(height), kRenderTextureFormat,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT)) {
    return 0;
  }
  rt.slot = next_slot_++;
  rt.targets = std::make_unique<Targets>();
  if (!CreateTargets(*rt.targets, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)})) {
    return 0;
  }
  // Until a camera draws it: white. (The image gets a defined layout the
  // first time RenderView sees it.)
  WriteTextureSlot(rt.slot, white_.View());
  const uint32_t slot = rt.slot;
  render_textures_.emplace(path, std::move(rt));
  return slot;
}

bool SceneRenderer::CreateWhiteTexture() { return true; }

void SceneRenderer::ReleaseUnused(VkCommandBuffer cmd) {
  // Images retired long enough ago: no frame in flight samples them now.
  std::erase_if(image_garbage_, [&](ImageGarbage& g) {
    if (g.frame + frames_in_flight_ + 1 > frame_number_) return false;
    WriteTextureSlot(g.slot, white_.View());
    free_slots_.push_back(g.slot);
    return true;
  });
  // Only now and then: the scans are cheap but not free.
  if (frame_number_ % 30 != 0) return;
  for (auto it = textures_.begin(); it != textures_.end();) {
    if (it->second.source.expired()) {
      image_garbage_.push_back({frame_number_, std::move(it->second.image), it->second.slot});
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
  // The buffers only grow at the end, so space is won back by starting
  // over: live geometry uploads again as it is drawn.
  constexpr uint32_t kInitialVertices = 262144;
  constexpr uint32_t kInitialIndices = 1048576;
  if (vertices_used_ <= kInitialVertices || live_vertices * 2 > vertices_used_) return;
  Buffer vertices, indices;
  if (!vertices.Create(context_->allocator, sizeof(ModelVertex) * kInitialVertices,
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       Buffer::Memory::kDeviceLocal) ||
      !indices.Create(context_->allocator, sizeof(uint32_t) * kInitialIndices,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      Buffer::Memory::kDeviceLocal)) {
    return;
  }
  (void)cmd;
  garbage_.push_back({frame_number_, std::move(vertex_buffer_)});
  garbage_.push_back({frame_number_, std::move(index_buffer_)});
  vertex_buffer_ = std::move(vertices);
  index_buffer_ = std::move(indices);
  vertex_capacity_ = kInitialVertices;
  index_capacity_ = kInitialIndices;
  vertices_used_ = 0;
  indices_used_ = 0;
  geometry_.clear();
}

// ----------------------------------------------------------------- Prepare --

void SceneRenderer::Prepare(VkCommandBuffer cmd, GameScene& scene) {
  FrameResources& frame = frames_[frame_index_];
  settings_ = scene.GetRenderSettings();
  lighting_ = scene.GetLighting();
  draws_.clear();
  records_.clear();
  bones_.clear();
  draw_entities_.clear();
  draw_skinned_.clear();

  if (white_.Get() == VK_NULL_HANDLE) {
    white_.Create(context_->allocator, context_->device, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);
    Buffer staging;
    const uint32_t pixel = 0xffffffffu;
    staging.Create(context_->allocator, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   Buffer::Memory::kHostVisible);
    staging.Write(&pixel, 4);
    TransitionImage(cmd, white_.Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging.Get(), white_.Get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    TransitionImage(cmd, white_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    frame.staging.push_back(std::move(staging));
    WriteTextureSlot(0, white_.View());
  }
  if (!lighting_initialized_) {
    // Nothing drawn yet: shadow cubes and probe faces start out readable,
    // the probe atlas empty (every probe unusable).
    for (VkImage image : {shadow_cubes_.Get(), probe_faces_.Get(), probe_visibility_.Get()}) {
      TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    TransitionImage(cmd, probe_atlas_.Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    TransitionImage(cmd, far_shadow_image_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    probe_sh_set_ = AllocatePostSet({probe_faces_.View()});
    probes_.clear_atlas = true;
    lighting_initialized_ = true;
  }
  AssignShadowCubes();
  ReleaseUnused(cmd);

  auto& registry = scene.GetRegistry();
  auto* map_scene = dynamic_cast<MapScene*>(&scene);
  auto view = registry.view<RenderMesh, Transform>();
  std::vector<glm::mat4> skin;
  for (auto [entity, mesh, transform] : view.each()) {
    if (!mesh.visible || registry.all_of<InactiveTag>(entity)) continue;
    uint32_t layer = 0;
    if (auto* info = registry.try_get<NodeInfo>(entity)) layer = static_cast<uint32_t>(info->layer);
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), transform.GetInterpolatedPosition()) *
                            transform.GetRotationMatrix() *
                            glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());
    const ModelPose* pose = nullptr;
    if (mesh.pose_entity != entt::null && registry.valid(mesh.pose_entity)) {
      pose = registry.try_get<ModelPose>(mesh.pose_entity);
    }
    for (const auto& draw : mesh.draws) {
      const GpuGeometry* geometry = UploadGeometry(cmd, frame, draw.geometry);
      if (!geometry) continue;
      static const RenderMaterial kDefaultMaterial;
      const RenderMaterial& material =
          mesh.materials.empty()
              ? kDefaultMaterial
              : mesh.materials[std::min<size_t>(draw.material, mesh.materials.size() - 1)];
      DrawData record = {};
      uint32_t bone_offset = kNoSkin;
      if (draw.skin >= 0) {
        mesh.SkinMatrices(draw, pose, skin);
        bone_offset = static_cast<uint32_t>(bones_.size());
        bones_.insert(bones_.end(), skin.begin(), skin.end());
        record.world = world;
      } else {
        record.world = world * mesh.DrawMatrix(draw, pose);
      }
      const glm::mat3 linear = glm::mat3(record.world);
      const float det = glm::determinant(linear);
      const bool mirrored = det < 0.0f;
      record.normal_matrix = glm::mat4(std::abs(det) > 1e-12f
                                           ? glm::transpose(glm::inverse(linear))
                                           : linear);
      record.color = glm::vec4(SrgbToLinear(glm::vec3(material.color)), material.color.a);
      record.emission = glm::vec4(material.emission, 0.0f);
      record.uv = glm::vec4(material.tiling, material.offset);
      uint32_t texture = 0;
      uint32_t flags = 0;
      if (!material.render_texture.empty()) {
        int width = 1024, height = 1024;
        if (map_scene) map_scene->GetRenderTextureSize(material.render_texture, width, height);
        texture = RenderTextureSlot(material.render_texture, width, height);
        flags |= kFlagClampTexture;
        // Rendered images start at the top row; Unity's start at the bottom.
        record.uv.y = -record.uv.y;
        record.uv.w = 1.0f - record.uv.w;
      } else {
        texture = TextureSlot(cmd, frame, material.texture);
      }
      if (material.unlit) flags |= kFlagUnlit;
      if (material.sdf_text) flags |= kFlagSdfText;
      if (material.transparent) flags |= kFlagTransparent;
      record.info = glm::uvec4(texture, flags, bone_offset, layer);
      record.material =
          glm::vec4(material.metallic, material.smoothness, material.normal_scale, 0.0f);
      record.maps = glm::uvec4(TextureSlot(cmd, frame, material.normal_texture),
                               TextureSlot(cmd, frame, material.emission_texture), 0, 0);

      FrameDraw frame_draw;
      frame_draw.geometry = geometry;
      frame_draw.record = static_cast<uint32_t>(records_.size());
      frame_draw.layer = layer;
      frame_draw.transparent = material.transparent;
      frame_draw.double_sided = material.double_sided || material.sdf_text;
      frame_draw.cast_shadows = mesh.cast_shadows && !material.transparent;
      frame_draw.outline = (mesh.rendering_layers & settings_.outline_layers) != 0;
      const glm::vec3 local_min = draw.geometry->bounds_min;
      const glm::vec3 local_max = draw.geometry->bounds_max;
      frame_draw.center = glm::vec3(record.world * glm::vec4((local_min + local_max) * 0.5f, 1.0f));
      frame_draw.bounds_min = glm::vec3(std::numeric_limits<float>::max());
      frame_draw.bounds_max = glm::vec3(-std::numeric_limits<float>::max());
      for (int corner = 0; corner < 8; corner++) {
        const glm::vec3 local((corner & 1) ? local_max.x : local_min.x,
                              (corner & 2) ? local_max.y : local_min.y,
                              (corner & 4) ? local_max.z : local_min.z);
        const glm::vec3 p = glm::vec3(record.world * glm::vec4(local, 1.0f));
        frame_draw.bounds_min = glm::min(frame_draw.bounds_min, p);
        frame_draw.bounds_max = glm::max(frame_draw.bounds_max, p);
      }
      if (draw.skin >= 0) {
        // Animated limbs reach past the bind pose's box.
        const glm::vec3 grow = (frame_draw.bounds_max - frame_draw.bounds_min) * 0.25f;
        frame_draw.bounds_min -= grow;
        frame_draw.bounds_max += grow;
      }
      draw_entities_.push_back(static_cast<uint32_t>(entity));
      draw_skinned_.push_back(draw.skin >= 0);
      // An odd number of negative scale axes reverses the winding.
      frame_draw.mirrored = mirrored;
      records_.push_back(record);
      draws_.push_back(frame_draw);
    }
  }

  TrackMotion();

  // Per-frame buffers grow when needed; this frame's earlier use is done.
  auto ensure = [&](Buffer& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage) {
    if (bytes <= buffer.Size()) return true;
    Buffer bigger;
    if (!bigger.Create(context_->allocator, std::max(bytes, buffer.Size() * 2), usage,
                       Buffer::Memory::kHostVisible)) {
      return false;
    }
    buffer = std::move(bigger);
    return true;
  };
  bool rebind = false;
  const VkDeviceSize draw_bytes = sizeof(DrawData) * std::max<size_t>(records_.size(), 1);
  const VkDeviceSize bone_bytes = sizeof(glm::mat4) * std::max<size_t>(bones_.size(), 1);
  if (draw_bytes > frame.draws.Size()) {
    rebind |= ensure(frame.draws, draw_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const VkDeviceSize count = frame.draws.Size() / sizeof(DrawData);
    if (ensure(frame.instances, count * sizeof(uint32_t), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
      std::vector<uint32_t> iota(count);
      for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
      frame.instances.Write(iota.data(), iota.size() * sizeof(uint32_t));
    }
  }
  if (bone_bytes > frame.bones.Size()) {
    rebind |= ensure(frame.bones, bone_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  }
  if (rebind) UpdateFrameSet(frame);
  if (!records_.empty()) frame.draws.Write(records_.data(), sizeof(DrawData) * records_.size());
  if (!bones_.empty()) frame.bones.Write(bones_.data(), sizeof(glm::mat4) * bones_.size());

  // Copies before any draw reads them.
  GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                    VK_ACCESS_2_SHADER_READ_BIT);
}

void SceneRenderer::TrackMotion() {
  // Each object's draws hashed together: a model's parts move with it.
  std::unordered_map<uint32_t, uint64_t> hashes;
  for (size_t i = 0; i < draws_.size(); i++) {
    auto [it, inserted] = hashes.try_emplace(draw_entities_[i], kHashSeed);
    HashFloats(it->second, &records_[draws_[i].record].world[0][0], 16);
  }
  size_t fresh = 0;
  for (const auto& [entity, hash] : hashes) fresh += motion_.count(entity) == 0;
  // Most objects new at once is a map loading: they start out still. One
  // that appears later (spawned) counts as moving until it has rested.
  const bool loading = fresh * 2 > hashes.size();
  for (const auto& [entity, hash] : hashes) {
    auto [it, inserted] = motion_.try_emplace(entity);
    Motion& motion = it->second;
    if (inserted) {
      motion.moved = loading ? 0 : frame_number_;
    } else if (motion.hash != hash) {
      motion.moved = frame_number_;
    }
    motion.hash = hash;
    motion.seen = frame_number_;
  }
  if (frame_number_ % 60 == 0) {
    std::erase_if(motion_, [&](const auto& entry) { return entry.second.seen + 300 < frame_number_; });
  }
  // Still for 2 seconds (at 60 fps) makes an object part of the scene again.
  constexpr uint64_t kRestFrames = 120;
  for (size_t i = 0; i < draws_.size(); i++) {
    const Motion& motion = motion_[draw_entities_[i]];
    draws_[i].dynamic =
        draw_skinned_[i] || (motion.moved != 0 && motion.moved + kRestFrames > frame_number_);
  }
}

// ----------------------------------------------------------------- Targets --

void SceneRenderer::WritePostSet(VkDescriptorSet set, const std::array<VkImageView, 6>& inputs) {
  VkDescriptorImageInfo samplers[2] = {
      {clamp_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
      {point_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
  };
  VkDescriptorImageInfo images[6];
  VkWriteDescriptorSet writes[8] = {};
  for (int i = 0; i < 8; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = set;
    writes[i].dstBinding = static_cast<uint32_t>(i);
    writes[i].descriptorCount = 1;
  }
  for (int i = 0; i < 2; i++) {
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[i].pImageInfo = &samplers[i];
  }
  for (int i = 0; i < 6; i++) {
    images[i] = {VK_NULL_HANDLE, inputs[i] ? inputs[i] : white_.View(),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[2 + i].pImageInfo = &images[i];
  }
  vkUpdateDescriptorSets(context_->device, 8, writes, 0, nullptr);
}

VkDescriptorSet SceneRenderer::AllocatePostSet(const std::array<VkImageView, 6>& inputs) {
  VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = pool_;
  alloc.descriptorSetCount = 1;
  alloc.pSetLayouts = &post_layout_;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (!VK_CHECK(vkAllocateDescriptorSets(context_->device, &alloc, &set))) return VK_NULL_HANDLE;
  WritePostSet(set, inputs);
  return set;
}

bool SceneRenderer::CreateTargets(Targets& targets, VkExtent2D extent) {
  targets.extent = extent;
  VmaAllocator a = context_->allocator;
  VkDevice d = context_->device;
  const VkImageUsageFlags color_usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  const VkExtent2D half = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
  targets.bloom_mips = std::max(1u, std::min(6u, static_cast<uint32_t>(std::floor(
                                                    std::log2(std::max(half.width, half.height)))) - 2));
  const bool ok =
      targets.hdr.Create(a, d, extent.width, extent.height, kHdrFormat, color_usage,
                         VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.normal.Create(a, d, extent.width, extent.height, kNormalFormat, color_usage,
                            VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.depth.Create(a, d, extent.width, extent.height, kDepthFormat,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT) &&
      targets.ao.Create(a, d, extent.width, extent.height, kMaskFormat, color_usage,
                        VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.ao_blur.Create(a, d, extent.width, extent.height, kMaskFormat, color_usage,
                             VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.bloom.Create(a, d, half.width, half.height, kHdrFormat, color_usage,
                           VK_IMAGE_ASPECT_COLOR_BIT, targets.bloom_mips) &&
      targets.outline.Create(a, d, extent.width, extent.height, kMaskFormat, color_usage,
                             VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.ldr.Create(a, d, extent.width, extent.height, kLdrFormat, color_usage,
                         VK_IMAGE_ASPECT_COLOR_BIT);
  if (!ok) {
    PDebug::error("Vulkan: could not create {}x{} render targets", extent.width, extent.height);
    return false;
  }
  targets.ssao_set = AllocatePostSet({targets.depth.View(), targets.normal.View()});
  targets.blur_set = AllocatePostSet({targets.ao.View(), targets.depth.View()});
  targets.bloom_down_sets.clear();
  targets.bloom_up_sets.clear();
  for (uint32_t mip = 0; mip < targets.bloom_mips; mip++) {
    VkImageView source = mip == 0 ? targets.hdr.View() : targets.bloom.MipView(mip - 1);
    targets.bloom_down_sets.push_back(AllocatePostSet({source}));
    // Upsampling into `mip` reads the next smaller one.
    VkImageView smaller = mip + 1 < targets.bloom_mips ? targets.bloom.MipView(mip + 1)
                                                       : targets.bloom.MipView(mip);
    targets.bloom_up_sets.push_back(AllocatePostSet({smaller}));
  }
  targets.composite_set =
      AllocatePostSet({targets.hdr.View(), targets.ao_blur.View(), targets.normal.View(),
                       targets.bloom.MipView(0), targets.outline.View()});
  targets.fxaa_set = AllocatePostSet({targets.ldr.View()});
  targets.sky_set = AllocatePostSet({});
  targets.sky_view = VK_NULL_HANDLE;
  return true;
}

void SceneRenderer::DestroyTargets(Targets& targets) {
  std::vector<VkDescriptorSet> sets = {targets.ssao_set, targets.blur_set, targets.composite_set,
                                       targets.fxaa_set, targets.sky_set};
  sets.insert(sets.end(), targets.bloom_down_sets.begin(), targets.bloom_down_sets.end());
  sets.insert(sets.end(), targets.bloom_up_sets.begin(), targets.bloom_up_sets.end());
  std::erase(sets, VK_NULL_HANDLE);
  if (!sets.empty()) {
    vkFreeDescriptorSets(context_->device, pool_, static_cast<uint32_t>(sets.size()), sets.data());
  }
  for (vk::Image* image : {&targets.hdr, &targets.normal, &targets.depth, &targets.ao,
                           &targets.ao_blur, &targets.bloom, &targets.outline, &targets.ldr}) {
    image->Reset();
  }
  targets.ssao_set = targets.blur_set = targets.composite_set = targets.fxaa_set =
      targets.sky_set = VK_NULL_HANDLE;
  targets.bloom_down_sets.clear();
  targets.bloom_up_sets.clear();
}

SceneRenderer::Targets& SceneRenderer::GetTargets(VkExtent2D extent) {
  for (auto& targets : targets_) {
    if (targets->extent.width == extent.width && targets->extent.height == extent.height) {
      targets->last_used = frame_number_;
      return *targets;
    }
  }
  // A new size (window resize): drop targets nothing has used for a while.
  std::erase_if(targets_, [&](const std::unique_ptr<Targets>& t) {
    if (t->last_used + 8 < frame_number_) {
      vkDeviceWaitIdle(context_->device);
      DestroyTargets(*t);
      return true;
    }
    return false;
  });
  auto targets = std::make_unique<Targets>();
  CreateTargets(*targets, extent);
  targets->last_used = frame_number_;
  targets_.push_back(std::move(targets));
  return *targets_.back();
}

// ----------------------------------------------------------------- Drawing --

uint32_t SceneRenderer::PushUniforms(FrameResources& frame, const FrameUniforms& uniforms) {
  if (frame.passes_used >= kMaxPasses) frame.passes_used = kMaxPasses - 1;
  const VkDeviceSize offset = uniform_stride_ * frame.passes_used++;
  frame.uniforms.Write(&uniforms, sizeof(uniforms), offset);
  return static_cast<uint32_t>(offset);
}

FrameUniforms SceneRenderer::CameraUniforms(const RenderCamera& camera, VkExtent2D extent,
                                            bool overlay, bool linear) const {
  FrameUniforms u = {};
  u.view = camera.view;
  u.proj = camera.projection;
  u.view_proj = camera.projection * camera.view;
  u.inv_view_proj = glm::inverse(u.view_proj);
  u.light_view_proj = light_view_proj_;
  u.light_dir = glm::vec4(lighting_.direction, 0.0f);
  u.light_color = glm::vec4(lighting_.color, 1.0f);
  // Light and ambient colors are linear.
  u.ambient_sky = glm::vec4(lighting_.ambient, 1.0f);
  u.ambient_ground = glm::vec4(lighting_.ambient_ground, 1.0f);
  u.camera_pos = glm::vec4(camera.position, 1.0f);
  u.resolution = glm::vec4(extent.width, extent.height, 1.0f / extent.width,
                           1.0f / extent.height);
  u.shadow_params = shadows_enabled_
                        ? glm::vec4(kShadowResolution, lighting_.shadow_normal_bias,
                                    lighting_.shadow_bias * 0.0f, 0.0f)
                        : glm::vec4(0.0f);
  u.flags = glm::vec4(linear ? 0.0f : 1.0f, overlay ? 1.0f : 0.0f, 0.0f, 0.0f);
  FillLighting(u);
  return u;
}

void SceneRenderer::RenderShadows(VkCommandBuffer cmd, const glm::mat4& light_view_proj,
                                  const RenderCamera& main_camera, bool enabled) {
  shadows_enabled_ = enabled;
  light_view_proj_ = light_view_proj;
  if (!shadow_written_) {
    TransitionImage(cmd, shadow_image_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    shadow_written_ = true;
  }
  FrameResources& frame = frames_[frame_index_];
  // The still, opaque geometry the main camera draws: what the probes and
  // the far shadow map see. Moving objects are left out, or neither would
  // ever settle.
  still_.clear();
  still_min_ = glm::vec3(std::numeric_limits<float>::max());
  still_max_ = glm::vec3(-std::numeric_limits<float>::max());
  still_indices_ = 0;
  for (const auto& draw : draws_) {
    if (draw.transparent || draw.dynamic ||
        !(main_camera.culling_mask & (1u << (draw.layer & 31)))) {
      continue;
    }
    still_.push_back(&draw);
    still_min_ = glm::min(still_min_, draw.bounds_min);
    still_max_ = glm::max(still_max_, draw.bounds_max);
    still_indices_ += draw.geometry->index_count;
  }
  if (enabled) {
    std::vector<const FrameDraw*> casters;
    for (const auto& draw : draws_) {
      if (draw.cast_shadows && (main_camera.culling_mask & (1u << (draw.layer & 31)))) {
        casters.push_back(&draw);
      }
    }
    DrawSunShadow(cmd, frame, shadow_image_, kShadowResolution, light_view_proj, casters);
  }
  // The probes see the scene lit, shadows included, so they come last.
  RenderShadowCubes(cmd, frame);
  UpdateFarShadow(cmd, frame);
  UpdateProbes(cmd, frame);
}

void SceneRenderer::UpdateFarShadow(VkCommandBuffer cmd, FrameResources& frame) {
  if (lighting_.color == glm::vec3(0.0f) || still_.empty()) {
    far_shadow_valid_ = false;
    return;
  }
  uint64_t hash = kHashSeed;
  HashFloats(hash, &lighting_.direction.x, 3);
  std::vector<const FrameDraw*> casters;
  for (const FrameDraw* draw : still_) {
    if (!draw->cast_shadows) continue;
    casters.push_back(draw);
    HashFloats(hash, &records_[draw->record].world[0][0], 16);
    const float geometry[2] = {static_cast<float>(draw->geometry->first_index),
                               static_cast<float>(draw->geometry->index_count)};
    HashFloats(hash, geometry, 2);
  }
  if (far_shadow_valid_ && hash == far_shadow_hash_) return;
  far_shadow_hash_ = hash;
  far_shadow_valid_ = true;

  // An orthographic box around all still geometry, tight in the sun's view.
  const glm::vec3 lo = still_min_ - 0.05f, hi = still_max_ + 0.05f;
  const glm::vec3 center = (lo + hi) * 0.5f;
  const float radius = glm::length(hi - lo) * 0.5f;
  const glm::vec3 dir = glm::normalize(lighting_.direction);
  const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  const glm::mat4 view = glm::lookAt(center - dir * radius * 2.0f, center, up);
  glm::vec3 box_min(std::numeric_limits<float>::max());
  glm::vec3 box_max(-std::numeric_limits<float>::max());
  for (int corner = 0; corner < 8; corner++) {
    const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                      (corner & 4) ? hi.z : lo.z);
    const glm::vec3 q = glm::vec3(view * glm::vec4(p, 1.0f));
    box_min = glm::min(box_min, q);
    box_max = glm::max(box_max, q);
  }
  far_view_proj_ = glm::orthoRH_ZO(box_min.x, box_max.x, box_min.y, box_max.y,
                                   -box_max.z - 0.01f, -box_min.z + 0.01f) *
                   view;
  // A coarser map needs a wider offset against acne.
  const float texel = std::max(box_max.x - box_min.x, box_max.y - box_min.y) /
                      static_cast<float>(kFarShadowResolution);
  far_normal_bias_ = std::max(lighting_.shadow_normal_bias, 2.0f * texel);
  DrawSunShadow(cmd, frame, far_shadow_image_, kFarShadowResolution, far_view_proj_, casters);
}

void SceneRenderer::DrawSunShadow(VkCommandBuffer cmd, FrameResources& frame, vk::Image& image,
                                  uint32_t size, const glm::mat4& view_proj,
                                  const std::vector<const FrameDraw*>& casters) {
  RenderCamera camera;
  FrameUniforms uniforms = CameraUniforms(camera, {size, size}, false, true);
  uniforms.view_proj = view_proj;
  const uint32_t offset = PushUniforms(frame, uniforms);

  DepthToAttachment(cmd, image.Get());
  VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = image.View();
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, {size, size}};
  rendering.layerCount = 1;
  rendering.pDepthAttachment = &depth;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, {size, size});
  if (!casters.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_);
    const VkDescriptorSet sets[2] = {frame.set, texture_set_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                            &offset);
    const VkBuffer buffers[2] = {vertex_buffer_.Get(), frame.instances.Get()};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
    FrontFace front_face;
    for (const FrameDraw* draw : casters) {
      front_face.Set(cmd, draw->mirrored);
      vkCmdDrawIndexed(cmd, draw->geometry->index_count, 1, draw->geometry->first_index,
                       static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
    }
  }
  vkCmdEndRendering(cmd);
  DepthToShaderRead(cmd, image.Get());
}

// ---------------------------------------------------------------- Lighting --

void SceneRenderer::AssignShadowCubes() {
  cubes_.clear();
  area_slots_.assign(lighting_.area_lights.size(), -1);
  point_slots_.assign(lighting_.point_lights.size(), -1);
  // Area lights first: they are usually the scene's main lights.
  for (size_t i = 0; i < lighting_.area_lights.size() && i < kMaxAreaLights; i++) {
    const AreaLightData& light = lighting_.area_lights[i];
    if (light.shadows == 0 || cubes_.size() >= kMaxShadowCubes) continue;
    area_slots_[i] = static_cast<int>(cubes_.size());
    // Just in front of the rectangle, so the emissive surface it often sits
    // on does not shadow everything.
    const glm::vec3 normal = light.Normal();
    cubes_.push_back({light.position + normal * (light.two_sided ? 0.0f : 0.02f), light.range,
                      !light.two_sided, normal});
  }
  for (size_t i = 0; i < lighting_.point_lights.size() && i < kMaxPointLights; i++) {
    const PointLightData& light = lighting_.point_lights[i];
    if (light.shadows == 0 || cubes_.size() >= kMaxShadowCubes) continue;
    point_slots_[i] = static_cast<int>(cubes_.size());
    cubes_.push_back({light.position, light.range});
  }
}

void SceneRenderer::FillLighting(FrameUniforms& u) const {
  const uint32_t point_count =
      std::min<uint32_t>(static_cast<uint32_t>(lighting_.point_lights.size()), kMaxPointLights);
  for (uint32_t i = 0; i < point_count; i++) {
    const PointLightData& light = lighting_.point_lights[i];
    const int slot = i < point_slots_.size() ? point_slots_[i] : -1;
    u.point_lights[2 * i] = glm::vec4(light.position, light.range);
    u.point_lights[2 * i + 1] = glm::vec4(light.color, static_cast<float>(slot));
    u.point_shadows[i] =
        glm::vec4(light.radius, light.shadow_bias, light.shadows == 2 ? 1.0f : 0.0f, 0.0f);
  }
  u.flags.w = static_cast<float>(point_count);
  const uint32_t area_count =
      std::min<uint32_t>(static_cast<uint32_t>(lighting_.area_lights.size()), kMaxAreaLights);
  for (uint32_t i = 0; i < area_count; i++) {
    const AreaLightData& light = lighting_.area_lights[i];
    const int slot = i < area_slots_.size() ? area_slots_[i] : -1;
    u.area_lights[4 * i] = glm::vec4(light.position, light.range);
    u.area_lights[4 * i + 1] = glm::vec4(light.right, light.two_sided ? 1.0f : 0.0f);
    u.area_lights[4 * i + 2] = glm::vec4(light.up, static_cast<float>(slot));
    u.area_lights[4 * i + 3] = glm::vec4(light.color, light.shadows == 2 ? 1.0f : 0.0f);
    u.area_shadow_bias[i] = glm::vec4(light.shadow_bias, 0.0f, 0.0f, 0.0f);
  }
  for (size_t i = 0; i < cubes_.size(); i++) {
    u.shadow_cubes_info[i] = glm::vec4(cubes_[i].center, cubes_[i].far_clip);
  }
  u.light_info = glm::vec4(static_cast<float>(area_count), 0.0f,
                           static_cast<float>(kShadowCubeResolution), 1.0f);
  if (far_shadow_valid_) {
    u.far_light_view_proj = far_view_proj_;
    u.far_shadow_params =
        glm::vec4(static_cast<float>(kFarShadowResolution), far_normal_bias_, 0.0f, 0.0f);
  }
  if (probes_.active) {
    u.gi_min = glm::vec4(probes_.min, 1.0f);
    u.gi_max = glm::vec4(probes_.max, probes_.intensity);
    u.gi_counts = glm::vec4(glm::vec3(probes_.counts), static_cast<float>(kProbesPerAtlasRow));
  }
}

FrameUniforms SceneRenderer::CubeFaceUniforms(glm::vec3 center, uint32_t face, float near_clip,
                                              float far_clip, VkExtent2D extent) const {
  RenderCamera camera;
  camera.view = glm::lookAt(center, center + kFaceForward[face], kFaceUp[face]);
  camera.projection = glm::perspective(glm::radians(90.0f), 1.0f, near_clip, far_clip);
  camera.position = center;
  return CameraUniforms(camera, extent, false, true);
}

void SceneRenderer::RenderShadowCubes(VkCommandBuffer cmd, FrameResources& frame) {
  if (cubes_.empty()) return;
  // Each slot's casters, and whether anything about them changed since the
  // slot was last drawn: still scenes draw their cubes once.
  std::vector<std::vector<const FrameDraw*>> casters(cubes_.size());
  std::vector<size_t> dirty;
  for (size_t slot = 0; slot < cubes_.size(); slot++) {
    const ShadowCube& cube = cubes_[slot];
    uint64_t hash = kHashSeed;
    const float values[8] = {cube.center.x, cube.center.y, cube.center.z, cube.far_clip,
                             cube.normal.x, cube.normal.y, cube.normal.z,
                             cube.one_sided ? 1.0f : 0.0f};
    HashFloats(hash, values, 8);
    for (size_t i = 0; i < draws_.size(); i++) {
      const FrameDraw& draw = draws_[i];
      if (!draw.cast_shadows || !BoxNear(draw.bounds_min, draw.bounds_max, cube.center,
                                         cube.far_clip)) {
        continue;
      }
      casters[slot].push_back(&draw);
      HashFloats(hash, &records_[draw.record].world[0][0], 16);
      const float geometry[2] = {static_cast<float>(draw.geometry->first_index),
                                 static_cast<float>(draw.geometry->index_count)};
      HashFloats(hash, geometry, 2);
      // Skinned meshes change shape without moving.
      if (draw_skinned_[i]) {
        const float frame_value = static_cast<float>(frame_number_);
        HashFloats(hash, &frame_value, 1);
      }
    }
    if (hash != cube_hashes_[slot]) {
      cube_hashes_[slot] = hash;
      dirty.push_back(slot);
    }
  }
  if (dirty.empty()) return;

  const VkExtent2D extent = {kShadowCubeResolution, kShadowCubeResolution};
  ToAttachment(cmd, shadow_cubes_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  for (size_t slot : dirty) {
    const ShadowCube& cube = cubes_[slot];
    for (uint32_t face = 0; face < 6; face++) {
      FrameUniforms uniforms = CubeFaceUniforms(cube.center, face, 0.005f, cube.far_clip, extent);
      uniforms.light_dir = glm::vec4(cube.normal, cube.one_sided ? 1.0f : 0.0f);
      const uint32_t offset = PushUniforms(frame, uniforms);
      TransitionImage(cmd, shadow_cube_depth_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      kDepthStages, kDepthAccess, kDepthStages, kDepthAccess);
      VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      color.imageView = shadow_cubes_.LayerView(static_cast<uint32_t>(slot) * 6 + face);
      color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      color.clearValue.color = {{1e30f, 0.0f, 0.0f, 0.0f}};  // nothing in the way
      VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      depth.imageView = shadow_cube_depth_.View();
      depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depth.clearValue.depthStencil = {1.0f, 0};
      VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
      rendering.renderArea = {{0, 0}, extent};
      rendering.layerCount = 1;
      rendering.colorAttachmentCount = 1;
      rendering.pColorAttachments = &color;
      rendering.pDepthAttachment = &depth;
      vkCmdBeginRendering(cmd, &rendering);
      SetViewport(cmd, extent);
      bool bound = false;
      for (const FrameDraw* draw : casters[slot]) {
        if (!FaceSees(cube.center, face, draw->bounds_min, draw->bounds_max, cube.far_clip)) {
          continue;
        }
        if (!bound) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_cube_);
          const VkDescriptorSet sets[2] = {frame.set, texture_set_};
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets,
                                  1, &offset);
          const VkBuffer buffers[2] = {vertex_buffer_.Get(), frame.instances.Get()};
          const VkDeviceSize offsets[2] = {0, 0};
          vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
          bound = true;
        }
        vkCmdDrawIndexed(cmd, draw->geometry->index_count, 1, draw->geometry->first_index,
                         static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
      }
      vkCmdEndRendering(cmd);
    }
  }
  ToShaderRead(cmd, shadow_cubes_.Get());
}

bool SceneRenderer::ResolveProbeVolume(ProbeVolumeData& volume) {
  const glm::vec3 still_min = still_min_, still_max = still_max_;
  volume = lighting_.probes;
  if (!volume.enabled) return false;
  if (!volume.automatic) {
    const int total = volume.counts.x * volume.counts.y * volume.counts.z;
    return total > 0 && total <= static_cast<int>(kMaxProbes);
  }
  if (glm::any(glm::greaterThan(still_min, still_max))) return false;  // nothing still

  // Refit only when the geometry outgrows the box or shrinks well inside it:
  // small changes keep the probes (a new grid starts from dark).
  const glm::vec3 old_extent = probes_.fit_max - probes_.fit_min;
  const glm::vec3 slack = old_extent * 0.02f + 1e-3f;
  const bool refit =
      !probes_.fitted ||
      glm::any(glm::lessThan(still_min, probes_.fit_min - slack)) ||
      glm::any(glm::greaterThan(still_max, probes_.fit_max + slack)) ||
      glm::any(glm::greaterThan(still_min - probes_.fit_min, old_extent * 0.15f + slack)) ||
      glm::any(glm::greaterThan(probes_.fit_max - still_max, old_extent * 0.15f + slack));
  if (refit) {
    probes_.fitted = true;
    probes_.fit_min = still_min;
    probes_.fit_max = still_max;
    // Flat scenes still get some height, so there is room above the floor.
    glm::vec3 extent = still_max - still_min;
    const float longest = std::max({extent.x, extent.y, extent.z, 0.1f});
    extent = glm::max(extent, glm::vec3(longest * 0.15f));
    // Even spacing, a probe's spacing past the geometry on every side, as
    // many probes as kAutoProbes allows.
    float spacing = std::cbrt(extent.x * extent.y * extent.z / kAutoProbes);
    glm::ivec3 counts;
    glm::vec3 span;
    for (int attempt = 0; attempt < 64; attempt++) {
      span = extent + spacing;
      counts = glm::clamp(glm::ivec3(glm::round(span / spacing)) + 1, glm::ivec3(2),
                          glm::ivec3(16));
      if (counts.x * counts.y * counts.z <= static_cast<int>(kAutoProbes)) break;
      spacing *= 1.08f;
    }
    const glm::vec3 center = (still_min + still_max) * 0.5f;
    probes_.grid_min = center - span * 0.5f;
    probes_.grid_max = center + span * 0.5f;
    probes_.grid_counts = counts;
  }
  volume.min = probes_.grid_min;
  volume.max = probes_.grid_max;
  volume.counts = probes_.grid_counts;
  return true;
}

void SceneRenderer::UpdateProbes(VkCommandBuffer cmd, FrameResources& frame) {
  const std::vector<const FrameDraw*>& still = still_;
  ProbeVolumeData volume;
  probes_.active = ResolveProbeVolume(volume);
  if (!probes_.active) return;
  probes_.intensity = volume.intensity;
  const uint32_t total = static_cast<uint32_t>(volume.counts.x * volume.counts.y *
                                               volume.counts.z);
  // A different volume starts over from empty probes.
  if (probes_.clear_atlas || volume.min != probes_.min || volume.max != probes_.max ||
      volume.counts != probes_.counts) {
    probes_.min = volume.min;
    probes_.max = volume.max;
    probes_.counts = volume.counts;
    probes_.next = 0;
    probes_.passes = 0;
    probes_.clear_atlas = false;
    TransitionImage(cmd, probe_atlas_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue zero = {};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, probe_atlas_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1,
                         &range);
    TransitionImage(cmd, probe_atlas_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }

  // Anything that changes the light the probes saw starts the bounces over
  // (keeping the old values, so the change fades in).
  uint64_t hash = kHashSeed;
  HashFloats(hash, &lighting_.direction.x, 12);  // direction, color and ambients
  for (const auto& light : lighting_.point_lights) {
    const float values[9] = {light.position.x, light.position.y, light.position.z,
                             light.color.r,    light.color.g,    light.color.b,
                             light.range,      static_cast<float>(light.shadows), light.radius};
    HashFloats(hash, values, 9);
  }
  for (const auto& light : lighting_.area_lights) {
    HashFloats(hash, &light.position.x, 12);  // position, right, up, color
    const float rest[3] = {light.range, light.two_sided ? 1.0f : 0.0f,
                           static_cast<float>(light.shadows)};
    HashFloats(hash, rest, 3);
  }
  for (const FrameDraw* draw : still) {
    // World matrix, color and emission (info and the rest never change alone).
    const DrawData& record = records_[draw->record];
    HashFloats(hash, &record.world[0][0], 16);
    HashFloats(hash, &record.color.x, 8);
    const float geometry = static_cast<float>(draw->geometry->first_index);
    HashFloats(hash, &geometry, 1);
  }
  if (hash != probes_.scene_hash) {
    probes_.scene_hash = hash;
    probes_.passes = 0;
  }
  if (probes_.passes >= volume.bounces) return;

  // Probes this frame: a budget of indices, about a third of the scene per
  // face once the faces cull what they cannot see.
  uint32_t batch = volume.probes_per_frame > 0
                       ? static_cast<uint32_t>(volume.probes_per_frame)
                       : static_cast<uint32_t>(std::max<uint64_t>(
                             1, kProbeIndexBudget / std::max<uint64_t>(2 * still_indices_, 1)));
  batch = std::min({batch, kMaxProbeBatch, total - probes_.next});

  const VkExtent2D extent = {kProbeFaceSize, kProbeFaceSize};
  const glm::vec3 cells = glm::max(glm::vec3(volume.counts - 1), glm::vec3(1.0f));
  // The sky a probe sees past everything is the ambient light: an alpha
  // past kProbeSkyAlpha asks the projection for the sky and ground colors.
  ToAttachment(cmd, probe_faces_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  for (uint32_t k = 0; k < batch; k++) {
    const uint32_t probe = probes_.next + k;
    const glm::ivec3 cell(probe % volume.counts.x, (probe / volume.counts.x) % volume.counts.y,
                          probe / (volume.counts.x * volume.counts.y));
    const glm::vec3 position = glm::mix(volume.min, volume.max, glm::vec3(cell) / cells);
    for (uint32_t face = 0; face < 6; face++) {
      FrameUniforms uniforms = CubeFaceUniforms(position, face, 0.01f, 1000.0f, extent);
      uniforms.light_info.y = 1.0f;  // capture
      uniforms.light_info.w = volume.include_emission ? 1.0f : 0.0f;
      // Shadowed by the scene-wide map alone, even when the camera's view
      // has no sun shadows: a sun shining through walls would light every room.
      uniforms.shadow_params.x = 0.0f;
      uniforms.far_shadow_params.w = 1.0f;
      const uint32_t offset = PushUniforms(frame, uniforms);
      ToAttachment(cmd, probe_normal_.Get(), VK_IMAGE_LAYOUT_UNDEFINED);
      TransitionImage(cmd, probe_depth_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      kDepthStages, kDepthAccess, kDepthStages, kDepthAccess);
      VkRenderingAttachmentInfo colors[2] = {};
      for (auto& c : colors) {
        c.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        c.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      }
      colors[0].imageView = probe_faces_.LayerView(k * 6 + face);
      colors[0].clearValue.color = {{0.0f, 0.0f, 0.0f, 65000.0f}};  // > kProbeSkyAlpha
      colors[1].imageView = probe_normal_.View();
      colors[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      depth.imageView = probe_depth_.View();
      depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depth.clearValue.depthStencil = {1.0f, 0};
      VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
      rendering.renderArea = {{0, 0}, extent};
      rendering.layerCount = 1;
      rendering.colorAttachmentCount = 2;
      rendering.pColorAttachments = colors;
      rendering.pDepthAttachment = &depth;
      vkCmdBeginRendering(cmd, &rendering);
      SetViewport(cmd, extent);
      bool bound = false;
      FrontFace front_face;
      for (const FrameDraw* draw : still) {
        if (!FaceSees(position, face, draw->bounds_min, draw->bounds_max, 1000.0f)) continue;
        if (!bound) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, probe_capture_);
          const VkDescriptorSet sets[2] = {frame.set, texture_set_};
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets,
                                  1, &offset);
          const VkBuffer buffers[2] = {vertex_buffer_.Get(), frame.instances.Get()};
          const VkDeviceSize offsets[2] = {0, 0};
          vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
          bound = true;
        }
        front_face.Set(cmd, draw->mirrored);
        vkCmdDrawIndexed(cmd, draw->geometry->index_count, 1, draw->geometry->first_index,
                         static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
      }
      vkCmdEndRendering(cmd);
    }
  }
  ToShaderRead(cmd, probe_faces_.Get());

  // Each probe's 9 coefficients, in place in the atlas.
  ToAttachment(cmd, probe_atlas_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  const VkExtent2D atlas = probe_atlas_.Extent();
  VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = probe_atlas_.View();
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, atlas};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, atlas);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, probe_sh_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1,
                          &probe_sh_set_, 0, nullptr);
  for (uint32_t k = 0; k < batch; k++) {
    const uint32_t probe = probes_.next + k;
    const int32_t x = static_cast<int32_t>((probe % kProbesPerAtlasRow) * 9);
    const int32_t y = static_cast<int32_t>(probe / kProbesPerAtlasRow);
    const VkRect2D scissor = {{x, y}, {9, 1}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    PostPush push;
    push.p[0] = glm::vec4(static_cast<float>(k * 6), static_cast<float>(kProbeFaceSize),
                          static_cast<float>(x), static_cast<float>(y));
    push.p[1] = glm::vec4(0.25f, 0.0f, 0.0f, 0.0f);  // back faces above which it is inside
    push.p[2] = glm::vec4(lighting_.ambient, 0.0f);   // the sky, above
    push.p[3] = glm::vec4(lighting_.ambient_ground, 0.0f);  // and below
    vkCmdPushConstants(cmd, post_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }
  vkCmdEndRendering(cmd);
  ToShaderRead(cmd, probe_atlas_.Get());

  // And each probe's visibility map, from the same capture.
  ToAttachment(cmd, probe_visibility_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  const VkExtent2D visibility = probe_visibility_.Extent();
  color.imageView = probe_visibility_.View();
  rendering.renderArea = {{0, 0}, visibility};
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, visibility);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, probe_visibility_pass_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1,
                          &probe_sh_set_, 0, nullptr);
  // Open sky counts as a little past the farthest neighboring probe.
  const glm::vec3 spacing = (volume.max - volume.min) / cells;
  const float max_distance = 1.5f * glm::length(spacing);
  for (uint32_t k = 0; k < batch; k++) {
    const uint32_t probe = probes_.next + k;
    const int32_t x = static_cast<int32_t>((probe % kProbesPerAtlasRow) * kProbeVisibilitySize);
    const int32_t y = static_cast<int32_t>((probe / kProbesPerAtlasRow) * kProbeVisibilitySize);
    const VkRect2D scissor = {{x, y}, {kProbeVisibilitySize, kProbeVisibilitySize}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    PostPush push;
    push.p[0] = glm::vec4(static_cast<float>(k * 6), static_cast<float>(kProbeFaceSize),
                          static_cast<float>(x), static_cast<float>(y));
    push.p[1] = glm::vec4(max_distance, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(cmd, post_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
                       &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }
  vkCmdEndRendering(cmd);
  ToShaderRead(cmd, probe_visibility_.Get());

  probes_.next += batch;
  if (probes_.next >= total) {
    probes_.next = 0;
    probes_.passes++;
    if (probes_.passes == volume.bounces) {
      PDebug::log("Light probes: {} probes settled after {} bounces (frame {})", total,
                  volume.bounces, frame_number_);
    }
  }
}

void SceneRenderer::DrawList(VkCommandBuffer cmd, FrameResources& frame, uint32_t uniform_offset,
                             const std::vector<const FrameDraw*>& list, bool transparent_pass) {
  if (list.empty()) return;
  const VkDescriptorSet sets[2] = {frame.set, texture_set_};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                          &uniform_offset);
  const VkBuffer buffers[2] = {vertex_buffer_.Get(), frame.instances.Get()};
  const VkDeviceSize offsets[2] = {0, 0};
  vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
  vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
  VkPipeline bound = VK_NULL_HANDLE;
  FrontFace front_face;
  for (const FrameDraw* draw : list) {
    VkPipeline pipeline = transparent_pass ? transparent_[draw->double_sided ? 1 : 0]
                                           : opaque_[draw->double_sided ? 1 : 0];
    if (pipeline != bound) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      bound = pipeline;
    }
    front_face.Set(cmd, draw->mirrored);
    vkCmdDrawIndexed(cmd, draw->geometry->index_count, 1, draw->geometry->first_index,
                     static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
  }
}

void SceneRenderer::DrawCamera(VkCommandBuffer cmd, FrameResources& frame,
                               const RenderCamera& camera, Targets& targets, bool clear_color,
                               bool clear_depth, bool overlay, bool linear,
                               const LegacyDraws* legacy, VkImageView sky_view, bool has_sky) {
  const VkExtent2D extent = targets.extent;
  const FrameUniforms uniforms = CameraUniforms(camera, extent, overlay, linear);
  const uint32_t offset = PushUniforms(frame, uniforms);

  ToAttachment(cmd, targets.hdr.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ToAttachment(cmd, targets.normal.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  DepthToAttachment(cmd, targets.depth.Get());

  // The background color is sRGB, like Unity's; linear passes decode it.
  glm::vec4 background = camera.background;
  if (linear) background = glm::vec4(SrgbToLinear(glm::vec3(background)), background.a);
  VkRenderingAttachmentInfo colors[2] = {};
  for (auto& c : colors) {
    c.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    c.loadOp = clear_color ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  }
  colors[0].imageView = targets.hdr.View();
  colors[0].clearValue.color = {{background.r, background.g, background.b, 1.0f}};
  colors[1].imageView = targets.normal.View();
  colors[1].clearValue.color = {{0.5f, 0.5f, 1.0f, 0.0f}};
  VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = targets.depth.View();
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 2;
  rendering.pColorAttachments = colors;
  rendering.pDepthAttachment = &depth;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, extent);

  if (clear_color && camera.clear == static_cast<int>(CameraClear::kSkybox)) {
    if (targets.sky_view != sky_view) {
      targets.sky_view = sky_view;
      WritePostSet(targets.sky_set, {sky_view});
    }
    PostPush push;
    const glm::mat4 sky = glm::inverse(camera.projection * glm::mat4(glm::mat3(camera.view)));
    for (int i = 0; i < 4; i++) push.p[i] = sky[i];
    push.p[4] = glm::vec4(has_sky ? 1.0f : 0.0f, 1.0f, linear ? 0.0f : 1.0f, 0.0f);
    push.p[5] = glm::vec4(lighting_.direction, 0.0f);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1,
                            &targets.sky_set, 0, nullptr);
    vkCmdPushConstants(cmd, post_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  if (legacy && *legacy) (*legacy)(cmd, extent);

  std::vector<const FrameDraw*> opaque, transparent;
  for (const auto& draw : draws_) {
    if (!(camera.culling_mask & (1u << (draw.layer & 31)))) continue;
    (draw.transparent ? transparent : opaque).push_back(&draw);
  }
  std::stable_sort(opaque.begin(), opaque.end(), [](const FrameDraw* a, const FrameDraw* b) {
    return a->double_sided < b->double_sided;
  });
  std::sort(transparent.begin(), transparent.end(), [&](const FrameDraw* a, const FrameDraw* b) {
    return glm::distance(a->center, camera.position) > glm::distance(b->center, camera.position);
  });
  DrawList(cmd, frame, offset, opaque, false);
  DrawList(cmd, frame, offset, transparent, true);
  vkCmdEndRendering(cmd);

  ToShaderRead(cmd, targets.hdr.Get());
  ToShaderRead(cmd, targets.normal.Get());
  DepthToShaderRead(cmd, targets.depth.Get());
}

void SceneRenderer::DrawOutlineMask(VkCommandBuffer cmd, FrameResources& frame,
                                    const RenderCamera& camera, Targets& targets,
                                    const RenderSettings& settings) {
  std::vector<const FrameDraw*> outlined;
  for (const auto& draw : draws_) {
    if (draw.outline && (camera.culling_mask & (1u << (draw.layer & 31)))) {
      outlined.push_back(&draw);
    }
  }
  ToAttachment(cmd, targets.outline.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = targets.outline.View();
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, targets.extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  vkCmdBeginRendering(cmd, &rendering);
  if (!outlined.empty()) {
    SetViewport(cmd, targets.extent);
    const uint32_t offset =
        PushUniforms(frame, CameraUniforms(camera, targets.extent, false, true));
    const VkDescriptorSet sets[2] = {frame.set, texture_set_};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outline_mask_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                            &offset);
    const VkBuffer buffers[2] = {vertex_buffer_.Get(), frame.instances.Get()};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
    FrontFace front_face;
    for (const FrameDraw* draw : outlined) {
      front_face.Set(cmd, draw->mirrored);
      vkCmdDrawIndexed(cmd, draw->geometry->index_count, 1, draw->geometry->first_index,
                       static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
    }
  }
  vkCmdEndRendering(cmd);
  ToShaderRead(cmd, targets.outline.Get());
}

void SceneRenderer::Fullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
                               VkImageView target, VkExtent2D extent, const void* params,
                               bool clear, bool load) {
  VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = target;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD
                      : (clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE);
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, extent};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, extent);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1, &set,
                          0, nullptr);
  vkCmdPushConstants(cmd, post_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                     sizeof(PostPush), params);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRendering(cmd);
}

void SceneRenderer::PostProcess(VkCommandBuffer cmd, Targets& targets, const RenderCamera& camera,
                                const RenderSettings& settings, VkImage output,
                                VkImageView output_view, VkFormat output_format,
                                VkImageLayout output_layout, bool srgb_output) {
  const VkExtent2D extent = targets.extent;
  const bool post = settings.post_processing && camera.post_processing;

  // Bloom
  const bool bloom = post && settings.bloom_intensity > 0.0f;
  if (bloom) {
    VkExtent2D size = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
    std::vector<VkExtent2D> sizes;
    for (uint32_t mip = 0; mip < targets.bloom_mips; mip++) {
      sizes.push_back(size);
      size = {std::max(1u, size.width / 2), std::max(1u, size.height / 2)};
    }
    VkExtent2D source = extent;
    for (uint32_t mip = 0; mip < targets.bloom_mips; mip++) {
      TransitionImageMips(cmd, targets.bloom.Get(), VK_IMAGE_ASPECT_COLOR_BIT, mip, 1,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess);
      PostPush push;
      push.p[0] = glm::vec4(1.0f / source.width, 1.0f / source.height, mip == 0 ? 1.0f : 0.0f, 0);
      push.p[1] = glm::vec4(settings.bloom_threshold, 0.5f, 0.0f, 0.0f);
      Fullscreen(cmd, bloom_down_, targets.bloom_down_sets[mip], targets.bloom.MipView(mip),
                 sizes[mip], &push, false);
      TransitionImageMips(cmd, targets.bloom.Get(), VK_IMAGE_ASPECT_COLOR_BIT, mip, 1,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      source = sizes[mip];
    }
    for (int mip = static_cast<int>(targets.bloom_mips) - 2; mip >= 0; mip--) {
      TransitionImageMips(cmd, targets.bloom.Get(), VK_IMAGE_ASPECT_COLOR_BIT, mip, 1,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess);
      PostPush push;
      const VkExtent2D smaller = sizes[mip + 1];
      push.p[0] = glm::vec4(1.0f / smaller.width, 1.0f / smaller.height,
                            settings.bloom_scatter, 0.0f);
      Fullscreen(cmd, bloom_up_, targets.bloom_up_sets[mip], targets.bloom.MipView(mip),
                 sizes[mip], &push, false, true);
      TransitionImageMips(cmd, targets.bloom.Get(), VK_IMAGE_ASPECT_COLOR_BIT, mip, 1,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess,
                          VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
  }

  // Composite (and FXAA)
  PostPush push;
  // AO and FXAA run without post-processing too (on the display-ready
  // image), for the screen's camera only (RenderTextures have no AO pass).
  const bool effects = camera.post_processing && !srgb_output;
  const bool ao = effects && settings.ssao_intensity > 0.0f;
  // Without post-processing the scene shaders already tonemapped (3), except
  // into RenderTextures, which stay linear (0).
  const float tonemap = post ? static_cast<float>(settings.tonemap) : (srgb_output ? 0.0f : 3.0f);
  push.p[0] = glm::vec4(ao ? settings.ssao_intensity : 0.0f,
                        bloom ? settings.bloom_intensity : 0.0f, tonemap, settings.exposure);
  push.p[1] = glm::vec4(post ? settings.vignette_intensity : 0.0f, settings.vignette_smoothness,
                        settings.vignette_center);
  push.p[2] = settings.outline_color;
  push.p[3] = glm::vec4(post && settings.outline_layers != 0 ? settings.outline_width : 0.0f,
                        0.0f, 0.0f, 0.0f);
  // An sRGB target encodes by itself (p4.z).
  push.p[4] = glm::vec4(1.0f / extent.width, 1.0f / extent.height, srgb_output ? 1.0f : 0.0f,
                        0.0f);
  const bool fxaa = effects && settings.fxaa;
  if (fxaa) {
    ToAttachment(cmd, targets.ldr.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Fullscreen(cmd, PipelineForFormat(kLdrFormat, false), targets.composite_set,
               targets.ldr.View(), extent, &push, false);
    ToShaderRead(cmd, targets.ldr.Get());
  }
  TransitionImage(cmd, output, VK_IMAGE_ASPECT_COLOR_BIT, output_layout,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kColorAccess,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kColorAccess);
  if (fxaa) {
    PostPush fxaa_push;
    fxaa_push.p[0] = glm::vec4(1.0f / extent.width, 1.0f / extent.height, 0.0f, 0.0f);
    Fullscreen(cmd, PipelineForFormat(output_format, true), targets.fxaa_set, output_view,
               extent, &fxaa_push, false);
  } else {
    Fullscreen(cmd, PipelineForFormat(output_format, false), targets.composite_set, output_view,
               extent, &push, false);
  }
}

void SceneRenderer::InitTargets(VkCommandBuffer cmd, Targets& targets) {
  // Every target rests in SHADER_READ_ONLY between passes; each frame
  // redraws them completely, so their old contents can be discarded.
  for (vk::Image* image : {&targets.hdr, &targets.normal, &targets.ao, &targets.ao_blur,
                           &targets.bloom, &targets.outline, &targets.ldr}) {
    TransitionImage(cmd, image->Get(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kColorAccess,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  TransitionImage(cmd, targets.depth.Get(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | kDepthStages,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | kDepthAccess,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void SceneRenderer::RenderView(VkCommandBuffer cmd, GameScene& scene,
                               const std::vector<RenderCamera>& cameras, VkImage output,
                               VkImageView output_view, VkExtent2D extent,
                               VkFormat output_format, VkImageLayout output_layout,
                               const LegacyDraws& legacy, VkImageView sky_texture,
                               VkSampler sky_sampler) {
  FrameResources& frame = frames_[frame_index_];
  const RenderCamera* main = nullptr;
  std::vector<const RenderCamera*> overlays;
  for (const auto& camera : cameras) {
    if (!camera.target_texture.empty()) {
      // RenderTexture cameras draw first, into their texture.
      RenderTextureSlot(camera.target_texture, camera.target_width, camera.target_height);
      auto it = render_textures_.find(camera.target_texture);
      if (it == render_textures_.end() || !it->second.targets) continue;
      auto& rt = it->second;
      InitTargets(cmd, *rt.targets);
      DrawCamera(cmd, frame, camera, *rt.targets, true, true, false, true, nullptr, sky_texture,
                 sky_texture != VK_NULL_HANDLE);
      PostProcess(cmd, *rt.targets, camera, settings_, rt.color.Get(), rt.color.View(),
                  kRenderTextureFormat,
                  rt.drawn ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                  true);
      ToShaderRead(cmd, rt.color.Get());
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

  Targets& targets = GetTargets(extent);
  InitTargets(cmd, targets);
  const bool post = settings_.post_processing && main->post_processing;
  const bool clears = main->clear != static_cast<int>(CameraClear::kNothing) &&
                      main->clear != static_cast<int>(CameraClear::kDepthOnly);
  DrawCamera(cmd, frame, *main, targets, true, true, false, post, &legacy, sky_texture,
             sky_texture != VK_NULL_HANDLE && clears);

  if (main->post_processing && settings_.ssao_intensity > 0.0f) {
    PostPush push;
    const glm::mat4& p = main->projection;
    push.p[0] = glm::vec4(1.0f / p[0][0], 1.0f / p[1][1], p[2][2], p[3][2]);
    push.p[1] = glm::vec4(settings_.ssao_radius, settings_.ssao_intensity,
                          static_cast<float>(settings_.ssao_samples), 0.0f);
    push.p[2] = glm::vec4(p[0][0], p[1][1], 0.0f, 0.0f);
    ToAttachment(cmd, targets.ao.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Fullscreen(cmd, ssao_, targets.ssao_set, targets.ao.View(), extent, &push, false);
    ToShaderRead(cmd, targets.ao.Get());
    PostPush blur;
    blur.p[0] = glm::vec4(1.0f / extent.width, 1.0f / extent.height, 0.0f, 0.0f);
    ToAttachment(cmd, targets.ao_blur.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Fullscreen(cmd, blur_, targets.blur_set, targets.ao_blur.View(), extent, &blur, false);
    ToShaderRead(cmd, targets.ao_blur.Get());
  }
  if (post && settings_.outline_layers != 0) {
    DrawOutlineMask(cmd, frame, *main, targets, settings_);
  }
  for (const RenderCamera* overlay : overlays) {
    const bool clear_color = overlay->clear == static_cast<int>(CameraClear::kColor) ||
                             overlay->clear == static_cast<int>(CameraClear::kSkybox);
    DrawCamera(cmd, frame, *overlay, targets, clear_color, true, true, post, nullptr,
               sky_texture, false);
  }
  PostProcess(cmd, targets, *main, settings_, output, output_view, output_format, output_layout,
              false);
}

}  // namespace Mercury::vk
