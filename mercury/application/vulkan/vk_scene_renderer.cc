#include "vk_scene_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>

#include "components/components.h"
#include "debugger/debugger.h"
#include "debugger/profiler.h"
#include "os/filesystem.h"
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

// Must match kCull* in cull.cs.hlsl.
constexpr uint32_t kCullAlive = 1;
constexpr uint32_t kCullTransparent = 2;
constexpr uint32_t kCullDoubleSided = 4;
constexpr uint32_t kCullMirrored = 8;
constexpr uint32_t kCullCastShadows = 16;
constexpr uint32_t kCullDynamic = 32;
constexpr uint32_t kCullBuckets = 4;
// Must match kView* in cull.cs.hlsl.
constexpr uint32_t kViewOneBucket = 1;
constexpr uint32_t kViewSizeTest = 2;
constexpr VkDeviceSize kCommandSize = sizeof(VkDrawIndexedIndirectCommand);

struct CullPush {
  glm::vec4 planes[6];
  glm::uvec4 view;    // slots, first command, first count, commands per bucket
  glm::uvec4 filter;  // mask, value, layers
};
static_assert(sizeof(CullPush) == 128);

// The six planes (pointing inside) of the frustum `view_proj` sees, in
// world space, for depth 0..1.
void FrustumPlanes(const glm::mat4& m, glm::vec4 (&planes)[6]) {
  const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
  const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
  const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
  const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
  planes[0] = row3 + row0;
  planes[1] = row3 - row0;
  planes[2] = row3 + row1;
  planes[3] = row3 - row1;
  planes[4] = row2;
  planes[5] = row3 - row2;
  for (auto& plane : planes) {
    const float length = glm::length(glm::vec3(plane));
    if (length > 0.0f) plane /= length;
  }
}

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
  if (frames_in_flight > probes_.batches.size()) {
    PDebug::error("Vulkan: the model renderer supports up to {} frames in flight",
                  probes_.batches.size());
    return false;
  }
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(context.physical_device, &properties);
  const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
  uniform_stride_ = (sizeof(FrameUniforms) + alignment - 1) / alignment * alignment;

  // The opaque geometry's depth first, so the lighting runs once per pixel
  // (MERCURY_DEPTH_PREPASS=0 or 1 overrides when it pays).
  if (const char* prepass = std::getenv("MERCURY_DEPTH_PREPASS")) {
    depth_prepass_ = std::string(prepass) != "0" ? 1 : 0;
  }
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
        {11, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
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
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(std::size(bindings));
    info.pBindings = bindings;
    if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &cull_layout_))) {
      return false;
    }
    const VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPush)};
    VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &cull_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    if (!VK_CHECK(vkCreatePipelineLayout(device, &layout, nullptr, &cull_pipeline_layout_))) {
      return false;
    }
  }
  {
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frames_in_flight_},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frames_in_flight_ * 8},
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
  VkShaderModule probe_octa_ps = shaders("probe_octa.ps.spv");
  auto destroy = [&] {
    for (VkShaderModule m : {model_vs, model_ps, shadow_vs, outline_ps, sky_ps, ssao_ps,
                             blur_ps, down_ps, up_ps, shadow_cube_ps, probe_sh_ps,
                             probe_visibility_ps, probe_octa_ps}) {
      if (m) vkDestroyShaderModule(device, m, nullptr);
    }
  };
  if (!model_vs || !model_ps || !shadow_vs || !outline_ps || !fullscreen_vs_ || !sky_ps ||
      !ssao_ps || !blur_ps || !down_ps || !up_ps || !composite_ps_ || !fxaa_ps_ ||
      !shadow_cube_ps || !probe_sh_ps || !probe_visibility_ps || !probe_octa_ps) {
    destroy();
    return false;
  }
  const std::vector<VkFormat> main_colors = {kHdrFormat, kNormalFormat};
  const VkCullModeFlags culls[2] = {VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_NONE};
  bool ok = true;
  for (int i = 0; i < 2; i++) {
    opaque_[i] = BuildPipeline(model_vs, model_ps, true, main_colors, kDepthFormat, 0, true,
                               true, VK_COMPARE_OP_LESS_OR_EQUAL, culls[i], false, model_layout_);
    prepass_[i] = BuildPipeline(model_vs, VK_NULL_HANDLE, true, {}, kDepthFormat, 0, true, true,
                                VK_COMPARE_OP_LESS_OR_EQUAL, culls[i], false, model_layout_);
    ok = ok && prepass_[i];
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
  shadow_cube_ = BuildPipeline(model_vs, shadow_cube_ps, true, {}, kDepthFormat, 0, true, true,
                               VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE, false,
                               model_layout_);
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
  probe_octa_ = BuildPipeline(fullscreen_vs_, probe_octa_ps, false, {kProbeFormat, kProbeFormat},
                              VK_FORMAT_UNDEFINED, 0, false, false, VK_COMPARE_OP_ALWAYS,
                              VK_CULL_MODE_NONE, false, post_pipeline_layout_);
  ok = ok && shadow_ && outline_mask_ && sky_ && ssao_ && blur_ && bloom_down_ && bloom_up_ &&
       shadow_cube_ && probe_capture_ && probe_sh_ && probe_visibility_pass_ && probe_octa_;
  destroy();
  if (ok && context_->gpu_driven) {
    // Without it every view draws from the CPU lists.
    if (VkShaderModule cull_cs = shaders("cull.cs.spv")) {
      VkComputePipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
      info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
      info.stage.module = cull_cs;
      info.stage.pName = "Main";
      info.layout = cull_pipeline_layout_;
      if (!VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &cull_))) {
        cull_ = VK_NULL_HANDLE;
      }
      vkDestroyShaderModule(device, cull_cs, nullptr);
    }
  }
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
  constexpr uint32_t kInitialDraws = 4096;
  if (!draw_buffer_.Create(context_->allocator, sizeof(DrawData) * kInitialDraws,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           Buffer::Memory::kDeviceLocal) ||
      !instance_buffer_.Create(context_->allocator, sizeof(uint32_t) * kInitialDraws,
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Buffer::Memory::kHostVisible)) {
    return false;
  }
  {
    std::vector<uint32_t> iota(kInitialDraws);
    for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
    instance_buffer_.Write(iota.data(), iota.size() * sizeof(uint32_t));
  }
  if (!cull_buffer_.Create(context_->allocator, sizeof(CullData) * kInitialDraws,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           Buffer::Memory::kDeviceLocal)) {
    return false;
  }
  frames_.resize(frames_in_flight_);
  for (auto& frame : frames_) {
    using Memory = Buffer::Memory;
    if (!frame.uniforms.Create(context_->allocator, uniform_stride_ * kMaxPasses,
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Memory::kHostVisible) ||
        !frame.bones.Create(context_->allocator, sizeof(glm::mat4) * 4096,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Memory::kHostVisible)) {
      return false;
    }
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
  VkDescriptorBufferInfo draws = {draw_buffer_.Get(), 0, VK_WHOLE_SIZE};
  frame.bound_draws = draw_buffer_.Get();
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
  VkDescriptorImageInfo atlas = {VK_NULL_HANDLE, probe_irradiance_.View(),
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo radiance = {VK_NULL_HANDLE, probe_radiance_.View(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo far_shadow = {VK_NULL_HANDLE, far_shadow_image_.View(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo visibility = {VK_NULL_HANDLE, probe_visibility_.View(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet writes[12] = {};
  for (int i = 0; i < 12; i++) {
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
  writes[11].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  writes[11].pImageInfo = &radiance;
  vkUpdateDescriptorSets(context_->device, 12, writes, 0, nullptr);
}

uint32_t SceneRenderer::SlotCapacity() const {
  return static_cast<uint32_t>(draw_buffer_.Size() / sizeof(DrawData));
}

bool SceneRenderer::CreateCullResources() {
  FrameResources& frame = frames_[frame_index_];
  const VkDeviceSize commands = kCullArenaCommands;
  if (frame.indirect.Size() < commands * kCommandSize) {
    Buffer indirect;
    if (!indirect.Create(context_->allocator, commands * kCommandSize,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                         Buffer::Memory::kDeviceLocal)) {
      return false;
    }
    garbage_.push_back({frame_number_, std::move(frame.indirect)});
    frame.indirect = std::move(indirect);
    frame.bound_cull = VK_NULL_HANDLE;
  }
  if (!frame.counts.Valid() &&
      !frame.counts.Create(context_->allocator, sizeof(uint32_t) * kMaxCullCounts,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           Buffer::Memory::kDeviceLocal)) {
    return false;
  }
  if (!frame.cull_set) {
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &cull_layout_;
    if (!VK_CHECK(vkAllocateDescriptorSets(context_->device, &alloc, &frame.cull_set))) {
      return false;
    }
    frame.bound_cull = VK_NULL_HANDLE;
  }
  if (frame.bound_cull == cull_buffer_.Get() && frame.cull_set) return true;
  frame.bound_cull = cull_buffer_.Get();
  const VkDescriptorBufferInfo buffers[3] = {{cull_buffer_.Get(), 0, VK_WHOLE_SIZE},
                                             {frame.indirect.Get(), 0, VK_WHOLE_SIZE},
                                             {frame.counts.Get(), 0, VK_WHOLE_SIZE}};
  VkWriteDescriptorSet writes[3] = {};
  for (uint32_t i = 0; i < 3; i++) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = frame.cull_set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &buffers[i];
  }
  vkUpdateDescriptorSets(context_->device, 3, writes, 0, nullptr);
  return true;
}

SceneRenderer::CulledView SceneRenderer::CullView(VkCommandBuffer cmd, FrameResources& frame,
                                                  const glm::mat4& view_proj, uint32_t mask,
                                                  uint32_t value, uint32_t layers,
                                                  const CullOptions& options) {
  CulledView view;
  const uint32_t slots = static_cast<uint32_t>(cull_records_.size());
  view.buckets = options.one_bucket ? 1 : kCullBuckets;
  view.stride = std::max(slots, 1u);
  if (!cull_ || !frame.cull_set ||
      frame.cull_commands + view.buckets * view.stride > kCullArenaCommands ||
      frame.cull_counts + view.buckets > kMaxCullCounts) {
    return {};
  }
  view.valid = true;
  view.command_base = frame.cull_commands;
  view.count_base = frame.cull_counts;
  frame.cull_commands += view.buckets * view.stride;
  frame.cull_counts += view.buckets;

  vkCmdFillBuffer(cmd, frame.counts.Get(), sizeof(uint32_t) * view.count_base,
                  sizeof(uint32_t) * view.buckets, 0);
  GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
  CullPush push = {};
  FrustumPlanes(view_proj, push.planes);
  uint32_t view_flags = options.one_bucket ? kViewOneBucket : 0u;
  if (options.min_size > 0.0f) {
    view_flags |= kViewSizeTest;
    push.planes[5] = glm::vec4(options.eye, options.min_size);
  }
  push.view = glm::uvec4(slots, view.command_base, view.count_base, view.stride);
  push.filter = glm::uvec4(mask, value, layers, view_flags);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_layout_, 0, 1,
                          &frame.cull_set, 0, nullptr);
  vkCmdPushConstants(cmd, cull_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push),
                     &push);
  if (slots > 0) vkCmdDispatch(cmd, (slots + 63) / 64, 1, 1);
  GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
  return view;
}

void SceneRenderer::DrawCulled(VkCommandBuffer cmd, FrameResources& frame, const CulledView& view,
                               uint32_t uniform_offset, VkPipeline single,
                               VkPipeline double_sided) {
  const VkDescriptorSet sets[2] = {frame.set, texture_set_};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                          &uniform_offset);
  const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
  const VkDeviceSize offsets[2] = {0, 0};
  vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
  vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
  VkPipeline bound = VK_NULL_HANDLE;
  for (uint32_t bucket = 0; bucket < view.buckets; bucket++) {
    VkPipeline pipeline = (bucket & 1) ? double_sided : single;
    if (pipeline != bound) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      bound = pipeline;
    }
    vkCmdSetFrontFace(cmd, (bucket & 2) ? VK_FRONT_FACE_CLOCKWISE
                                        : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdDrawIndexedIndirectCount(
        cmd, frame.indirect.Get(), kCommandSize * (view.command_base + bucket * view.stride),
        frame.counts.Get(), sizeof(uint32_t) * (view.count_base + bucket), view.stride,
        static_cast<uint32_t>(kCommandSize));
  }
}

bool SceneRenderer::CreateShadowMap() {
  return shadow_image_.Create(context_->allocator, context_->device, kShadowResolution,
                              kShadowResolution, kDepthFormat,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT) &&
         shadow_cache_.Create(context_->allocator, context_->device, kShadowResolution,
                              kShadowResolution, kDepthFormat,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
}

bool SceneRenderer::CreateLightingResources() {
  VkFormatProperties format;
  vkGetPhysicalDeviceFormatProperties(context_->physical_device, kProbeVisibilityFormat, &format);
  visibility_filterable_ =
      (format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
  VmaAllocator a = context_->allocator;
  VkDevice d = context_->device;
  const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!context_->image_cube_array) {
    PDebug::error("Vulkan: the GPU has no cube map arrays (imageCubeArray), which light "
                  "shadows need");
    return false;
  }
  if (!shadow_cubes_.CreateCubeArray(a, d, kShadowCubeResolution, kMaxShadowCubes, kDepthFormat,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                     VK_IMAGE_ASPECT_DEPTH_BIT) ||
      // Copied in and out for the probe cache.
      !probe_atlas_.Create(a, d, kProbesPerAtlasRow * 9, kMaxProbes / kProbesPerAtlasRow,
                           kProbeFormat,
                           color | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_irradiance_.Create(a, d, kProbesPerAtlasRow * kProbeOctaTexels,
                                kMaxProbes / kProbesPerAtlasRow * kProbeOctaTexels, kProbeFormat,
                                color | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_radiance_.Create(a, d, kProbesPerAtlasRow * kProbeOctaTexels,
                              kMaxProbes / kProbesPerAtlasRow * kProbeOctaTexels, kProbeFormat,
                              color | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_faces_.CreateArray(a, d, kProbeFaceSize, kProbeFaceSize, kMaxProbeBatch * 6,
                                kHdrFormat, color, VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_normal_.Create(a, d, kProbeFaceSize, kProbeFaceSize, kNormalFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT) ||
      !probe_depth_.Create(a, d, kProbeFaceSize, kProbeFaceSize, kDepthFormat,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT) ||
      !probe_visibility_.Create(a, d, kProbesPerAtlasRow * kProbeVisibilityTexels,
                                kMaxProbes / kProbesPerAtlasRow * kProbeVisibilityTexels,
                                kProbeVisibilityFormat,
                                color | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                VK_IMAGE_ASPECT_COLOR_BIT) ||
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
  shadow_cache_.Reset();
  shadow_cubes_.Reset();
  far_shadow_image_.Reset();
  probe_visibility_.Reset();
  probe_atlas_.Reset();
  probe_irradiance_.Reset();
  probe_radiance_.Reset();
  probe_faces_.Reset();
  probe_normal_.Reset();
  probe_depth_.Reset();
  vertex_buffer_.Reset();
  index_buffer_.Reset();
  draw_buffer_.Reset();
  instance_buffer_.Reset();
  cull_buffer_.Reset();
  cull_records_.clear();
  if (cull_) vkDestroyPipeline(device, cull_, nullptr);
  cull_ = VK_NULL_HANDLE;
  if (cull_pipeline_layout_) vkDestroyPipelineLayout(device, cull_pipeline_layout_, nullptr);
  if (cull_layout_) vkDestroyDescriptorSetLayout(device, cull_layout_, nullptr);
  cull_pipeline_layout_ = VK_NULL_HANDLE;
  cull_layout_ = VK_NULL_HANDLE;
  // The registry may be gone already; its hooks just stop forwarding.
  if (hooks_) hooks_->renderer = nullptr;
  hooks_.reset();
  registry_ = nullptr;
  draws_.clear();
  records_.clear();
  auto destroy_pipeline = [&](VkPipeline& p) {
    if (p) vkDestroyPipeline(device, p, nullptr);
    p = VK_NULL_HANDLE;
  };
  for (int i = 0; i < 2; i++) {
    destroy_pipeline(opaque_[i]);
    destroy_pipeline(prepass_[i]);
    destroy_pipeline(transparent_[i]);
  }
  for (VkPipeline* p : {&shadow_, &outline_mask_, &sky_, &ssao_, &blur_, &bloom_down_,
                        &bloom_up_, &shadow_cube_, &probe_capture_, &probe_sh_,
                        &probe_visibility_pass_, &probe_octa_}) {
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
  frames_[frame_index].cull_commands = 0;
  frames_[frame_index].cull_counts = 0;
  std::erase_if(garbage_, [&](const Garbage& g) {
    return g.frame + frames_in_flight_ + 1 <= frame_number_;
  });
  FinishProbeSave();
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
  // Every slot named the old buffers: upload and build them again.
  rebuild_all_ = true;
}

// ----------------------------------------------------------------- Prepare --

void SceneRenderer::Prepare(VkCommandBuffer cmd, GameScene& scene) {
  FrameResources& frame = frames_[frame_index_];
  settings_ = scene.GetRenderSettings();
  lighting_ = scene.GetLighting();
  bones_.clear();

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
    TransitionImage(cmd, shadow_cubes_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    for (VkImage image : {probe_faces_.Get(), probe_visibility_.Get()}) {
      TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    for (VkImage image : {probe_atlas_.Get(), probe_irradiance_.Get(), probe_radiance_.Get()}) {
      TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    TransitionImage(cmd, far_shadow_image_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    probe_sh_set_ = AllocatePostSet({probe_faces_.View()});
    probe_octa_set_ = AllocatePostSet({probe_atlas_.View()});
    probes_.clear_atlas = true;
    lighting_initialized_ = true;
  }
  AssignShadowCubes();
  ReleaseUnused(cmd);
  {
    MERCURY_PROFILE_SCOPE("sync_scene");
    SyncScene(cmd, frame, scene);
  }
  MERCURY_PROFILE_SCOPE("update_dynamic");
  UpdateDynamic();
  {
    uint64_t live = 0, triangles = 0;
    for (const auto& draw : draws_) {
      if (!draw.alive) continue;
      live++;
      triangles += draw.index_count / 3;
    }
    Profiler::SetCounter("draws", static_cast<double>(live));
    Profiler::SetCounter("triangles", static_cast<double>(triangles));
    live_triangles_ = triangles;
  }

  // Bone matrices are this frame's alone; the buffer grows when needed
  // (this frame's earlier use of it is done).
  const VkDeviceSize bone_bytes = sizeof(glm::mat4) * std::max<size_t>(bones_.size(), 1);
  bool rebind = false;
  if (bone_bytes > frame.bones.Size()) {
    Buffer bigger;
    if (bigger.Create(context_->allocator, std::max(bone_bytes, frame.bones.Size() * 2),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Buffer::Memory::kHostVisible)) {
      frame.bones = std::move(bigger);
      rebind = true;
    }
  }
  if (!bones_.empty()) frame.bones.Write(bones_.data(), sizeof(glm::mat4) * bones_.size());
  UploadRecords(cmd, frame);
  if (rebind || frame.bound_draws != draw_buffer_.Get()) UpdateFrameSet(frame);
  if (cull_ && !CreateCullResources()) {
    PDebug::error("Vulkan: could not create the culling buffers; culling on the CPU");
    vkDestroyPipeline(context_->device, cull_, nullptr);
    cull_ = VK_NULL_HANDLE;
  }

  // Copies before any draw (or culling) reads them.
  GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                    VK_ACCESS_2_SHADER_READ_BIT);
}

// ------------------------------------------------------------ Scene slots --

void RenderHooks::OnMeshChanged(entt::registry& registry, entt::entity entity) {
  if (renderer && &registry == renderer->registry_) renderer->changed_.push_back(entity);
}

void RenderHooks::OnProxyDestroyed(entt::registry& registry, entt::entity entity) {
  if (renderer && &registry == renderer->registry_) {
    renderer->FreeProxySlots(registry.get<RenderProxy>(entity));
  }
}

void SceneRenderer::AttachRegistry(entt::registry& registry) {
  // The previous scene's registry may be gone: let go of it without
  // touching it.
  if (hooks_) hooks_->renderer = nullptr;
  FreeAllSlots();
  registry_ = &registry;
  if (auto* existing = registry.try_ctx<std::shared_ptr<RenderHooks>>()) {
    hooks_ = *existing;
  } else {
    hooks_ = std::make_shared<RenderHooks>();
    registry.set<std::shared_ptr<RenderHooks>>(hooks_);
    // Rebuilds a RenderMesh added, replaced (emplace_or_replace, patch) or
    // removed, and entities turning active or inactive. A RenderMesh edited
    // in place needs registry.patch<RenderMesh>(entity) to be seen.
    registry.on_construct<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    registry.on_update<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    registry.on_destroy<RenderMesh>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    registry.on_construct<InactiveTag>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    registry.on_destroy<InactiveTag>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    // A node's layer (MapScene::SetLayer patches it).
    registry.on_update<NodeInfo>().connect<&RenderHooks::OnMeshChanged>(*hooks_);
    registry.on_destroy<RenderProxy>().connect<&RenderHooks::OnProxyDestroyed>(*hooks_);
  }
  // Proxies left from an earlier visit name slots that are gone.
  registry.clear<RenderProxy>();
  hooks_->renderer = this;
  changed_.clear();
}

void SceneRenderer::FreeAllSlots() {
  draws_.clear();
  records_.clear();
  cull_records_.clear();
  record_dirty_.clear();
  dirty_records_.clear();
  free_draw_slots_.clear();
  static_version_++;
}

uint32_t SceneRenderer::AllocateSlot() {
  if (!free_draw_slots_.empty()) {
    const uint32_t slot = free_draw_slots_.back();
    free_draw_slots_.pop_back();
    return slot;
  }
  draws_.emplace_back();
  records_.emplace_back();
  cull_records_.emplace_back();
  record_dirty_.push_back(0);
  return static_cast<uint32_t>(draws_.size() - 1);
}

void SceneRenderer::MarkSlotDirty(uint32_t slot) {
  if (record_dirty_[slot]) return;
  record_dirty_[slot] = 1;
  dirty_records_.push_back(slot);
}

void SceneRenderer::WriteCullData(uint32_t slot) {
  const FrameDraw& draw = draws_[slot];
  CullData& data = cull_records_[slot];
  data = {};
  if (draw.alive) {
    data.center = glm::vec4((draw.bounds_min + draw.bounds_max) * 0.5f, 0.0f);
    data.extent = glm::vec4((draw.bounds_max - draw.bounds_min) * 0.5f, 0.0f);
    data.index_count = draw.index_count;
    data.first_index = draw.first_index;
    data.vertex_offset = static_cast<int32_t>(draw.geometry->first_vertex);
    data.flags = kCullAlive | (draw.transparent ? kCullTransparent : 0u) |
                 (draw.double_sided ? kCullDoubleSided : 0u) |
                 (draw.mirrored ? kCullMirrored : 0u) |
                 (draw.cast_shadows ? kCullCastShadows : 0u) |
                 (draw.dynamic ? kCullDynamic : 0u) | ((draw.layer & 31u) << 8);
  }
  MarkSlotDirty(slot);
}

void SceneRenderer::FreeProxySlots(RenderProxy& proxy) {
  for (uint32_t slot : proxy.slots) {
    if (slot >= draws_.size()) continue;
    if (!draws_[slot].dynamic) static_version_++;
    draws_[slot] = FrameDraw{};
    WriteCullData(slot);
    free_draw_slots_.push_back(slot);
  }
  proxy.slots.clear();
}

void SceneRenderer::SyncScene(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene) {
  auto& registry = scene.GetRegistry();
  if (&registry != registry_) AttachRegistry(registry);

  // What the signals reported: rebuild those, and drop the proxies of
  // entities that no longer have a RenderMesh.
  for (entt::entity entity : changed_) {
    if (!registry.valid(entity)) continue;
    auto* proxy = registry.try_get<RenderProxy>(entity);
    if (!proxy) continue;
    if (!registry.all_of<RenderMesh>(entity)) {
      registry.remove<RenderProxy>(entity);  // frees its slots
    } else {
      proxy->dirty = true;
    }
  }
  changed_.clear();
  if (rebuild_all_) {
    rebuild_all_ = false;
    for (auto [entity, proxy] : registry.view<RenderProxy>().each()) proxy.dirty = true;
  }

  // New entities. Most of the scene new at once is a map loading: it starts
  // out still. One that appears later (spawned) counts as moving until it
  // has rested.
  std::vector<entt::entity> fresh;
  for (auto entity : registry.view<RenderMesh, Transform>(entt::exclude<RenderProxy>)) {
    fresh.push_back(entity);
  }
  const size_t existing = registry.view<RenderProxy>().size();
  const bool loading = fresh.size() * 2 > existing + fresh.size();
  for (entt::entity entity : fresh) {
    RenderProxy& proxy = registry.emplace<RenderProxy>(entity);
    if (!loading) proxy.moved = frame_number_;
  }

  // Every drawn entity: only what moved or changed is rebuilt.
  for (auto [entity, mesh, transform, proxy] :
       registry.view<RenderMesh, Transform, RenderProxy>().each()) {
    const glm::vec3 position = transform.GetInterpolatedPosition();
    const glm::vec3 rotation = transform.GetInterpolatedRotation();
    const glm::vec3 scale = transform.GetInterpolatedScale();
    const bool moved = proxy.built && (position != proxy.position || rotation != proxy.rotation ||
                                       scale != proxy.scale);
    if (!proxy.dirty && !proxy.animated && !moved && mesh.visible == proxy.mesh_visible &&
        mesh.cast_shadows == proxy.cast_shadows &&
        mesh.rendering_layers == proxy.rendering_layers) {
      continue;
    }
    if (proxy.dirty || !proxy.built) {
      proxy.inactive = registry.all_of<InactiveTag>(entity);
      proxy.layer = 0;
      if (auto* info = registry.try_get<NodeInfo>(entity)) {
        proxy.layer = static_cast<uint32_t>(info->layer);
      }
    }
    if (moved) proxy.moved = frame_number_;
    proxy.position = position;
    proxy.rotation = rotation;
    proxy.scale = scale;
    proxy.mesh_visible = mesh.visible;
    proxy.visible = mesh.visible && !proxy.inactive;
    proxy.cast_shadows = mesh.cast_shadows;
    proxy.rendering_layers = mesh.rendering_layers;
    BuildProxy(cmd, frame, scene, entity, mesh, transform, proxy);
  }
}

void SceneRenderer::BuildProxy(VkCommandBuffer cmd, FrameResources& frame, GameScene& scene,
                               entt::entity entity, const RenderMesh& mesh,
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

  // The slots it needs: one per draw with geometry, or per cluster of a
  // large one; none when hidden.
  struct Piece {
    const RenderDraw* draw;
    const MeshCluster* cluster;  // null: all of it
  };
  std::vector<Piece> drawn;
  if (proxy.visible) {
    for (const auto& draw : mesh.draws) {
      if (!draw.geometry || draw.geometry->indices.empty()) continue;
      if (draw.geometry->clusters.empty()) {
        drawn.push_back({&draw, nullptr});
      } else {
        for (const auto& cluster : draw.geometry->clusters) drawn.push_back({&draw, &cluster});
      }
    }
  }
  while (proxy.slots.size() > drawn.size()) {
    const uint32_t slot = proxy.slots.back();
    proxy.slots.pop_back();
    if (!draws_[slot].dynamic) static_version_++;
    draws_[slot] = FrameDraw{};
    WriteCullData(slot);
    free_draw_slots_.push_back(slot);
  }
  while (proxy.slots.size() < drawn.size()) proxy.slots.push_back(AllocateSlot());

  constexpr uint64_t kRestFrames = 120;
  const bool resting = proxy.moved == 0 || proxy.moved + kRestFrames <= frame_number_;
  // Still geometry changed: what the probes and shadow caches saw is stale.
  if (!proxy.animated && resting) static_version_++;
  if (drawn.empty()) return;

  auto* map_scene = dynamic_cast<MapScene*>(&scene);
  const glm::mat4 world = glm::translate(glm::mat4(1.0f), transform.GetInterpolatedPosition()) *
                          transform.GetRotationMatrix() *
                          glm::scale(glm::mat4(1.0f), transform.GetInterpolatedScale());
  std::vector<glm::mat4> skin;
  for (size_t i = 0; i < drawn.size(); i++) {
    const RenderDraw& draw = *drawn[i].draw;
    const MeshCluster* cluster = drawn[i].cluster;
    const uint32_t slot = proxy.slots[i];
    const GpuGeometry* geometry = UploadGeometry(cmd, frame, draw.geometry);
    FrameDraw& frame_draw = draws_[slot];
    if (!geometry) {
      frame_draw = FrameDraw{};  // kept (unused) until the entity changes
      WriteCullData(slot);
      continue;
    }
    static const RenderMaterial kDefaultMaterial;
    const RenderMaterial& material =
        mesh.materials.empty()
            ? kDefaultMaterial
            : mesh.materials[std::min<size_t>(draw.material, mesh.materials.size() - 1)];
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
    const glm::mat3 linear = glm::mat3(record.world);
    const float det = glm::determinant(linear);
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
    record.info = glm::uvec4(texture, flags, bone_offset, proxy.layer);
    record.material =
        glm::vec4(material.metallic, material.smoothness, material.normal_scale, 0.0f);
    record.maps = glm::uvec4(TextureSlot(cmd, frame, material.normal_texture),
                             TextureSlot(cmd, frame, material.emission_texture), 0, 0);
    const bool was_dynamic = frame_draw.alive && frame_draw.dynamic;
    frame_draw = FrameDraw{};
    frame_draw.geometry = geometry;
    frame_draw.record = slot;
    frame_draw.layer = proxy.layer;
    frame_draw.entity = static_cast<uint32_t>(entity);
    frame_draw.alive = true;
    frame_draw.transparent = material.transparent;
    frame_draw.double_sided = material.double_sided || material.sdf_text;
    frame_draw.cast_shadows = mesh.cast_shadows && !material.transparent;
    frame_draw.rendering_layers = mesh.rendering_layers;
    // An odd number of negative scale axes reverses the winding.
    frame_draw.mirrored = det < 0.0f;
    frame_draw.skinned = proxy.animated;
    frame_draw.moved = proxy.moved;
    frame_draw.dynamic = was_dynamic;  // UpdateDynamic decides
    frame_draw.first_index = geometry->first_index + (cluster ? cluster->first_index : 0);
    frame_draw.index_count = cluster ? cluster->index_count : geometry->index_count;
    const glm::vec3 local_min = cluster ? cluster->bounds_min : draw.geometry->bounds_min;
    const glm::vec3 local_max = cluster ? cluster->bounds_max : draw.geometry->bounds_max;
    frame_draw.center = glm::vec3(record.world * glm::vec4((local_min + local_max) * 0.5f, 1.0f));
    // The world box from the local one: each axis of the matrix adds its
    // extent (Arvo), no corners to transform.
    const glm::vec3 local_center = (local_min + local_max) * 0.5f;
    const glm::vec3 local_half = (local_max - local_min) * 0.5f;
    const glm::mat3 abs_linear(glm::abs(linear[0]), glm::abs(linear[1]), glm::abs(linear[2]));
    const glm::vec3 center = glm::vec3(record.world * glm::vec4(local_center, 1.0f));
    glm::vec3 half = abs_linear * local_half;
    if (draw.skin >= 0) half *= 1.5f;  // animated limbs reach past the bind pose's box
    frame_draw.bounds_min = center - half;
    frame_draw.bounds_max = center + half;
    WriteCullData(slot);
  }
}

void SceneRenderer::UpdateDynamic() {
  // Still for 2 seconds (at 60 fps) makes an object part of the scene again.
  constexpr uint64_t kRestFrames = 120;
  dynamic_casters_ = 0;
  for (uint32_t slot = 0; slot < draws_.size(); slot++) {
    FrameDraw& draw = draws_[slot];
    if (!draw.alive) continue;
    const bool dynamic =
        draw.skinned || (draw.moved != 0 && draw.moved + kRestFrames > frame_number_);
    if (dynamic != draw.dynamic) {
      draw.dynamic = dynamic;
      static_version_++;
      WriteCullData(slot);
    }
    dynamic_casters_ += dynamic && draw.cast_shadows;
  }
}

void SceneRenderer::UploadRecords(VkCommandBuffer cmd, FrameResources& frame) {
  const VkDeviceSize needed = sizeof(DrawData) * std::max<size_t>(records_.size(), 1);
  if (needed > draw_buffer_.Size()) {
    // A bigger buffer, filled from the CPU copy; frames in flight keep
    // reading the old one until they are done.
    Buffer bigger;
    const VkDeviceSize size = std::max(needed, draw_buffer_.Size() * 2);
    if (!bigger.Create(context_->allocator, size,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       Buffer::Memory::kDeviceLocal)) {
      PDebug::error("Vulkan: could not grow the draw buffer to {} bytes", size);
      return;
    }
    garbage_.push_back({frame_number_, std::move(draw_buffer_)});
    draw_buffer_ = std::move(bigger);
    const VkDeviceSize count = draw_buffer_.Size() / sizeof(DrawData);
    Buffer culls;
    if (culls.Create(context_->allocator, count * sizeof(CullData),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     Buffer::Memory::kDeviceLocal)) {
      garbage_.push_back({frame_number_, std::move(cull_buffer_)});
      cull_buffer_ = std::move(culls);
    }
    Buffer instances;
    if (instances.Create(context_->allocator, count * sizeof(uint32_t),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Buffer::Memory::kHostVisible)) {
      std::vector<uint32_t> iota(count);
      for (uint32_t i = 0; i < iota.size(); i++) iota[i] = i;
      instances.Write(iota.data(), iota.size() * sizeof(uint32_t));
      garbage_.push_back({frame_number_, std::move(instance_buffer_)});
      instance_buffer_ = std::move(instances);
    }
    for (size_t slot = 0; slot < records_.size(); slot++) {
      if (!record_dirty_[slot]) {
        record_dirty_[slot] = 1;
        dirty_records_.push_back(static_cast<uint32_t>(slot));
      }
    }
  }
  if (dirty_records_.empty()) return;

  // Runs of neighboring slots go in one copy each.
  std::sort(dirty_records_.begin(), dirty_records_.end());
  // Records first, then the cull data, in one staging buffer.
  const size_t count = dirty_records_.size();
  const VkDeviceSize cull_start = sizeof(DrawData) * count;
  Buffer staging;
  if (!staging.Create(context_->allocator, cull_start + sizeof(CullData) * count,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Buffer::Memory::kHostVisible)) {
    return;
  }
  auto* mapped = static_cast<uint8_t*>(staging.Mapped());
  std::vector<VkBufferCopy> regions, cull_regions;
  for (size_t i = 0; i < count; i++) {
    const uint32_t slot = dirty_records_[i];
    std::memcpy(mapped + sizeof(DrawData) * i, &records_[slot], sizeof(DrawData));
    std::memcpy(mapped + cull_start + sizeof(CullData) * i, &cull_records_[slot],
                sizeof(CullData));
    record_dirty_[slot] = 0;
    if (!regions.empty() && dirty_records_[i - 1] + 1 == slot) {
      regions.back().size += sizeof(DrawData);
      cull_regions.back().size += sizeof(CullData);
    } else {
      regions.push_back({sizeof(DrawData) * i, sizeof(DrawData) * slot, sizeof(DrawData)});
      cull_regions.push_back(
          {cull_start + sizeof(CullData) * i, sizeof(CullData) * slot, sizeof(CullData)});
    }
  }
  staging.Flush(0, staging.Size());
  dirty_records_.clear();
  // Earlier frames may still read the records being replaced.
  GlobalBarrier(cmd,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);
  vkCmdCopyBuffer(cmd, staging.Get(), draw_buffer_.Get(), static_cast<uint32_t>(regions.size()),
                  regions.data());
  vkCmdCopyBuffer(cmd, staging.Get(), cull_buffer_.Get(),
                  static_cast<uint32_t>(cull_regions.size()), cull_regions.data());
  frame.staging.push_back(std::move(staging));
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
      // Ambient occlusion at half resolution.
      targets.ao.Create(a, d, half.width, half.height, kMaskFormat, color_usage,
                        VK_IMAGE_ASPECT_COLOR_BIT) &&
      targets.ao_blur.Create(a, d, half.width, half.height, kMaskFormat, color_usage,
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
                       targets.bloom.MipView(0), targets.outline.View(), targets.depth.View()});
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
  light_view_proj_ = light_view_proj;  // UpdateSunShadow places its own
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
  if (still_version_ != static_version_ || still_mask_ != main_camera.culling_mask ||
      still_base_ != draws_.data()) {
    still_version_ = static_version_;
    still_mask_ = main_camera.culling_mask;
    still_base_ = draws_.data();
    still_.clear();
    still_min_ = glm::vec3(std::numeric_limits<float>::max());
    still_max_ = glm::vec3(-std::numeric_limits<float>::max());
    still_indices_ = 0;
    for (const auto& draw : draws_) {
      if (!draw.alive || draw.transparent || draw.dynamic ||
          !(main_camera.culling_mask & (1u << (draw.layer & 31)))) {
        continue;
      }
      still_.push_back(&draw);
      still_min_ = glm::min(still_min_, draw.bounds_min);
      still_max_ = glm::max(still_max_, draw.bounds_max);
      still_indices_ += draw.index_count;
    }
  }
  if (enabled) {
    GpuScope scope(gpu_timer_, cmd, "sun_shadow");
    UpdateSunShadow(cmd, frame, main_camera);
  }
  // The probes see the scene lit, shadows included, so they come last.
  {
    GpuScope scope(gpu_timer_, cmd, "shadow_cubes");
    RenderShadowCubes(cmd, frame);
  }
  {
    GpuScope scope(gpu_timer_, cmd, "far_shadow");
    UpdateFarShadow(cmd, frame);
  }
  {
    MERCURY_PROFILE_SCOPE("update_probes");
    GpuScope scope(gpu_timer_, cmd, "probes");
    UpdateProbes(cmd, frame);
  }
}

void SceneRenderer::UpdateFarShadow(VkCommandBuffer cmd, FrameResources& frame) {
  if (lighting_.color == glm::vec3(0.0f) || still_.empty()) {
    far_shadow_valid_ = false;
    return;
  }
  if (far_shadow_valid_ && far_shadow_version_ == static_version_ &&
      far_shadow_direction_ == lighting_.direction) {
    return;
  }
  far_shadow_version_ = static_version_;
  far_shadow_direction_ = lighting_.direction;
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
  // Still casters only, as still_ has them.
  const CulledView culled = CullView(
      cmd, frame, far_view_proj_,
      kCullAlive | kCullCastShadows | kCullDynamic | kCullTransparent,
      kCullAlive | kCullCastShadows, still_mask_, {});
  std::vector<const FrameDraw*> casters;
  if (!culled.valid) {
    for (const FrameDraw* draw : still_) {
      if (draw->cast_shadows) casters.push_back(draw);
    }
  }
  DrawSunShadow(cmd, frame, far_shadow_image_, kFarShadowResolution, far_view_proj_, casters,
                culled);
}

void SceneRenderer::UpdateSunShadow(VkCommandBuffer cmd, FrameResources& frame,
                                    const RenderCamera& camera) {
  glm::vec3 dir = lighting_.direction;
  dir = glm::length(dir) < 0.001f ? glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f))
                                  : glm::normalize(dir);
  const float distance = lighting_.shadow_distance > 0.0f ? lighting_.shadow_distance : 60.0f;
  const float half_box = distance * 0.6f;
  // The map's center moves in steps of a quarter of it (whole texels), so
  // between steps its still casters stay where they were drawn.
  const float step = half_box * 0.5f;
  const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  const glm::mat4 rotation = glm::lookAt(glm::vec3(0.0f), dir, up);
  const glm::vec3 eye = glm::vec3(rotation * glm::vec4(camera.position, 1.0f));
  ShadowCacheKey key;
  key.cell = glm::ivec3(glm::round(eye / step));
  key.direction = dir;
  key.distance = distance;
  key.static_version = static_version_;
  key.layers = camera.culling_mask;
  const glm::vec3 center = glm::vec3(key.cell) * step;
  const glm::mat4 view = glm::translate(glm::mat4(1.0f), -center) * rotation;
  // Depth from `distance` toward the sun to `distance` away from it.
  light_view_proj_ = glm::orthoRH_ZO(-half_box, half_box, -half_box, half_box, -distance,
                                     distance) *
                     view;

  const bool redraw = !shadow_cache_valid_ || !(key == shadow_cache_key_);
  if (redraw) {
    shadow_cache_key_ = key;
    shadow_cache_valid_ = true;
    const CulledView still = CullView(cmd, frame, light_view_proj_,
                                      kCullAlive | kCullCastShadows | kCullDynamic,
                                      kCullAlive | kCullCastShadows, camera.culling_mask, {});
    std::vector<const FrameDraw*> casters;
    if (!still.valid) {
      for (const auto& draw : draws_) {
        if (draw.alive && draw.cast_shadows && !draw.dynamic &&
            (camera.culling_mask & (1u << (draw.layer & 31)))) {
          casters.push_back(&draw);
        }
      }
    }
    DrawSunShadow(cmd, frame, shadow_cache_, kShadowResolution, light_view_proj_, casters, still,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  }
  // The live map: the cache, with this frame's moving casters over it. Left
  // alone while it already shows just the cache.
  const bool dynamic = dynamic_casters_ > 0;
  if (!redraw && !dynamic && !shadow_has_dynamic_) return;
  shadow_has_dynamic_ = dynamic;
  TransitionImage(cmd, shadow_image_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkImageCopy region = {};
  region.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  region.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
  region.extent = {kShadowResolution, kShadowResolution, 1};
  vkCmdCopyImage(cmd, shadow_cache_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 shadow_image_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  if (!dynamic) {
    TransitionImage(cmd, shadow_image_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return;
  }
  const CulledView moving = CullView(cmd, frame, light_view_proj_,
                                     kCullAlive | kCullCastShadows | kCullDynamic,
                                     kCullAlive | kCullCastShadows | kCullDynamic,
                                     camera.culling_mask, {});
  std::vector<const FrameDraw*> casters;
  if (!moving.valid) {
    for (const auto& draw : draws_) {
      if (draw.alive && draw.cast_shadows && draw.dynamic &&
          (camera.culling_mask & (1u << (draw.layer & 31)))) {
        casters.push_back(&draw);
      }
    }
  }
  DrawSunShadow(cmd, frame, shadow_image_, kShadowResolution, light_view_proj_, casters, moving,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SceneRenderer::DrawSunShadow(VkCommandBuffer cmd, FrameResources& frame, vk::Image& image,
                                  uint32_t size, const glm::mat4& view_proj,
                                  const std::vector<const FrameDraw*>& casters,
                                  const CulledView& culled, VkImageLayout from,
                                  VkImageLayout to) {
  RenderCamera camera;
  FrameUniforms uniforms = CameraUniforms(camera, {size, size}, false, true);
  uniforms.view_proj = view_proj;
  const uint32_t offset = PushUniforms(frame, uniforms);

  // Drawn over what a copy put there, or cleared.
  const bool load = from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  TransitionImage(cmd, image.Get(), VK_IMAGE_ASPECT_DEPTH_BIT, from,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
                      kDepthStages,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT |
                      VK_ACCESS_2_TRANSFER_WRITE_BIT | kDepthAccess,
                  kDepthStages, kDepthAccess);
  VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = image.View();
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth.clearValue.depthStencil = {1.0f, 0};
  VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, {size, size}};
  rendering.layerCount = 1;
  rendering.pDepthAttachment = &depth;
  vkCmdBeginRendering(cmd, &rendering);
  SetViewport(cmd, {size, size});
  if (culled.valid) {
    DrawCulled(cmd, frame, culled, offset, shadow_, shadow_);
  } else if (!casters.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_);
    const VkDescriptorSet sets[2] = {frame.set, texture_set_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                            &offset);
    const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
    FrontFace front_face;
    for (const FrameDraw* draw : casters) {
      front_face.Set(cmd, draw->mirrored);
      vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
                       static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
    }
  }
  vkCmdEndRendering(cmd);
  TransitionImage(cmd, image.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, to, kDepthStages, kDepthAccess,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
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
  u.gi_params = glm::vec4(visibility_filterable_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
  if (probes_.active) {
    u.gi_min = glm::vec4(probes_.min, 1.0f);
    u.gi_max = glm::vec4(probes_.max, probes_.intensity);
    u.gi_counts = glm::vec4(glm::vec3(probes_.counts), static_cast<float>(kProbesPerAtlasRow));
  }
}

FrameUniforms SceneRenderer::CubeFaceUniforms(glm::vec3 center, uint32_t face, float near_clip,
                                              float far_clip, VkExtent2D extent,
                                              bool cube_map) const {
  RenderCamera camera;
  camera.view = glm::lookAt(center, center + kFaceForward[face], kFaceUp[face]);
  camera.projection = glm::perspective(glm::radians(90.0f), 1.0f, near_clip, far_clip);
  if (cube_map) camera.projection = glm::scale(glm::mat4(1.0f), {-1.0f, 1.0f, 1.0f}) * camera.projection;
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
    // Still casters count through static_version_; moving ones near the
    // light one by one (one that leaves changes the set too).
    const float version = static_cast<float>(static_version_);
    HashFloats(hash, &version, 1);
    for (const FrameDraw& draw : draws_) {
      if (!draw.alive || !draw.cast_shadows ||
          !BoxNear(draw.bounds_min, draw.bounds_max, cube.center, cube.far_clip)) {
        continue;
      }
      casters[slot].push_back(&draw);
      if (!draw.dynamic) continue;
      HashFloats(hash, &records_[draw.record].world[0][0], 16);
      // Skinned meshes change shape without moving.
      if (draw.skinned) {
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
  DepthToAttachment(cmd, shadow_cubes_.Get());
  for (size_t slot : dirty) {
    const ShadowCube& cube = cubes_[slot];
    for (uint32_t face = 0; face < 6; face++) {
      FrameUniforms uniforms =
          CubeFaceUniforms(cube.center, face, 0.005f, cube.far_clip, extent, true);
      uniforms.light_dir = glm::vec4(cube.normal, cube.one_sided ? 1.0f : 0.0f);
      uniforms.light_info.z = cube.far_clip;  // depth is distance over this
      const uint32_t offset = PushUniforms(frame, uniforms);
      // Cleared to 1: nothing in the way.
      VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      depth.imageView = shadow_cubes_.LayerView(static_cast<uint32_t>(slot) * 6 + face);
      depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depth.clearValue.depthStencil = {1.0f, 0};
      VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
      rendering.renderArea = {{0, 0}, extent};
      rendering.layerCount = 1;
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
          const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
          const VkDeviceSize offsets[2] = {0, 0};
          vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
          bound = true;
        }
        vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
                         static_cast<int32_t>(draw->geometry->first_vertex), draw->record);
      }
      vkCmdEndRendering(cmd);
    }
  }
  DepthToShaderRead(cmd, shadow_cubes_.Get());
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
  // What this frame slot updated last time, which its GPU time measured.
  const uint32_t last_batch = probes_.batches[frame_index_];
  probes_.batches[frame_index_] = 0;
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
    probes_.check_cache = true;
    for (VkImage image : {probe_atlas_.Get(), probe_irradiance_.Get(), probe_radiance_.Get()}) {
      TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
      const VkClearColorValue zero = {};
      const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
      TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
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
  // Still geometry (moved, recolored, added or removed) through
  // static_version_.
  if (hash != probes_.light_hash || static_version_ != probes_.static_version) {
    probes_.light_hash = hash;
    probes_.static_version = static_version_;
    probes_.passes = 0;
    probes_.started = frame_number_;
    probes_.check_cache = true;
  }
  if (probes_.check_cache) {
    // Settled light for this very scene from an earlier run: no bounces.
    probes_.check_cache = false;
    probes_.cache_key = ProbeCacheKey(volume);
    if (LoadProbeCache(cmd, frame, probes_.cache_key, total)) {
      probes_.next = 0;
      probes_.passes = volume.bounces;
      PDebug::log("Light probes: {} probes from the cache", total);
      return;
    }
  }
  if (probes_.passes >= volume.bounces) return;

  // Probes this frame: a budget of indices, about a third of the scene per
  // face once the faces cull what they cannot see.
  uint32_t batch = volume.probes_per_frame > 0
                       ? static_cast<uint32_t>(volume.probes_per_frame)
                       : static_cast<uint32_t>(std::max<uint64_t>(
                             1, kProbeIndexBudget / std::max<uint64_t>(2 * still_indices_, 1)));
  // Measured: as many as fit kProbeGpuBudgetMs at what they cost when this
  // frame slot last ran (its timings just came back), growing gradually.
  const double last_ms = gpu_timer_ ? gpu_timer_->Last("probes") : 0.0;
  if (volume.probes_per_frame <= 0 && last_ms > 0.0 && last_batch > 0) {
    const double per_probe = last_ms / last_batch;
    batch = static_cast<uint32_t>(std::clamp(kProbeGpuBudgetMs / per_probe, 1.0,
                                             2.0 * last_batch));
  }
  batch = std::min({batch, kMaxProbeBatch, total - probes_.next});
  probes_.batches[frame_index_] = batch;

  const VkExtent2D extent = {kProbeFaceSize, kProbeFaceSize};
  const glm::vec3 cells = glm::max(glm::vec3(volume.counts - 1), glm::vec3(1.0f));
  // The sky a probe sees past everything is the ambient light: an alpha
  // past kProbeSkyAlpha asks the projection for the sky and ground colors.
  auto probe_position = [&](uint32_t probe) {
    const glm::ivec3 cell(probe % volume.counts.x, (probe / volume.counts.x) % volume.counts.y,
                          probe / (volume.counts.x * volume.counts.y));
    return glm::mix(volume.min, volume.max, glm::vec3(cell) / cells);
  };
  // Each face's still geometry, culled on the GPU first (outside the
  // passes). A face texel spans about 5.6 degrees: what is under half of
  // one would barely show, so it is left out.
  std::vector<CulledView> culled(size_t{batch} * 6);
  CullOptions capture_cull;
  capture_cull.one_bucket = true;
  capture_cull.min_size = 0.025f;
  for (uint32_t k = 0; k < batch; k++) {
    capture_cull.eye = probe_position(probes_.next + k);
    for (uint32_t face = 0; face < 6; face++) {
      const FrameUniforms uniforms = CubeFaceUniforms(capture_cull.eye, face, 0.01f, 1000.0f, extent);
      culled[k * 6 + face] =
          CullView(cmd, frame, uniforms.view_proj, kCullAlive | kCullTransparent | kCullDynamic,
                   kCullAlive, still_mask_, capture_cull);
    }
  }
  ToAttachment(cmd, probe_faces_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  for (uint32_t k = 0; k < batch; k++) {
    const glm::vec3 position = probe_position(probes_.next + k);
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
      if (const CulledView& view = culled[k * 6 + face]; view.valid) {
        DrawCulled(cmd, frame, view, offset, probe_capture_, probe_capture_);
        vkCmdEndRendering(cmd);
        continue;
      }
      bool bound = false;
      FrontFace front_face;
      for (const FrameDraw* draw : still) {
        if (!FaceSees(position, face, draw->bounds_min, draw->bounds_max, 1000.0f)) continue;
        if (!bound) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, probe_capture_);
          const VkDescriptorSet sets[2] = {frame.set, texture_set_};
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets,
                                  1, &offset);
          const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
          const VkDeviceSize offsets[2] = {0, 0};
          vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
          vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
          bound = true;
        }
        front_face.Set(cmd, draw->mirrored);
        vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
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
    const int32_t x = static_cast<int32_t>((probe % kProbesPerAtlasRow) * kProbeVisibilityTexels);
    const int32_t y = static_cast<int32_t>((probe / kProbesPerAtlasRow) * kProbeVisibilityTexels);
    const VkRect2D scissor = {{x, y}, {kProbeVisibilityTexels, kProbeVisibilityTexels}};
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

  // And the light maps shading reads, from the new coefficients.
  BuildProbeMaps(cmd, probes_.next, batch);

  probes_.next += batch;
  if (probes_.next >= total) {
    probes_.next = 0;
    probes_.passes++;
    if (probes_.passes == volume.bounces) {
      PDebug::log("Light probes: {} probes settled after {} bounces in {} frames", total,
                  volume.bounces, frame_number_ - probes_.started);
      SaveProbeCache(cmd, probes_.cache_key, total);
    }
  }
}

void SceneRenderer::BuildProbeMaps(VkCommandBuffer cmd, uint32_t first, uint32_t count) {
  ToAttachment(cmd, probe_irradiance_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ToAttachment(cmd, probe_radiance_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  {
    const VkExtent2D maps = probe_irradiance_.Extent();
    VkRenderingAttachmentInfo targets[2] = {};
    for (int i = 0; i < 2; i++) {
      targets[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      targets[i].imageView = i == 0 ? probe_irradiance_.View() : probe_radiance_.View();
      targets[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      targets[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      targets[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    VkRenderingInfo octa = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    octa.renderArea = {{0, 0}, maps};
    octa.layerCount = 1;
    octa.colorAttachmentCount = 2;
    octa.pColorAttachments = targets;
    vkCmdBeginRendering(cmd, &octa);
    SetViewport(cmd, maps);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, probe_octa_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout_, 0, 1,
                            &probe_octa_set_, 0, nullptr);
    for (uint32_t k = 0; k < count; k++) {
      const uint32_t probe = first + k;
      const int32_t x = static_cast<int32_t>((probe % kProbesPerAtlasRow) * kProbeOctaTexels);
      const int32_t y = static_cast<int32_t>((probe / kProbesPerAtlasRow) * kProbeOctaTexels);
      const VkRect2D scissor = {{x, y}, {kProbeOctaTexels, kProbeOctaTexels}};
      vkCmdSetScissor(cmd, 0, 1, &scissor);
      PostPush push;
      push.p[0] = glm::vec4(static_cast<float>((probe % kProbesPerAtlasRow) * 9),
                            static_cast<float>(probe / kProbesPerAtlasRow),
                            static_cast<float>(x), static_cast<float>(y));
      vkCmdPushConstants(cmd, post_pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(push), &push);
      vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRendering(cmd);
  }
  ToShaderRead(cmd, probe_irradiance_.Get());
  ToShaderRead(cmd, probe_radiance_.Get());
}

namespace {

// Must change when what a cached probe holds, or how probes are made, does.
constexpr uint32_t kProbeCacheVersion = 1;
constexpr char kProbeCacheMagic[4] = {'M', 'P', 'R', 'B'};

std::filesystem::path ProbeCacheFile(uint64_t key) {
  const std::filesystem::path dir = os::CacheDir();
  return dir.empty() ? dir : dir / "probes" / std::format("{:016x}.mprobe", key);
}

bool ProbeCacheEnabled() {
  const char* value = std::getenv("MERCURY_PROBE_CACHE");
  return !value || std::string(value) != "0";
}

}  // namespace

uint64_t SceneRenderer::ProbeCacheKey(const ProbeVolumeData& volume) const {
  uint64_t hash = kHashSeed;
  const float grid[13] = {volume.min.x, volume.min.y, volume.min.z, volume.max.x,
                          volume.max.y, volume.max.z, static_cast<float>(volume.counts.x),
                          static_cast<float>(volume.counts.y),
                          static_cast<float>(volume.counts.z),
                          static_cast<float>(volume.bounces),
                          volume.include_emission ? 1.0f : 0.0f,
                          static_cast<float>(kProbeCacheVersion),
                          static_cast<float>(still_mask_)};
  HashFloats(hash, grid, 13);
  // The lights (and sky) as UpdateProbes hashed them.
  const float light = static_cast<float>(probes_.light_hash % 16777216u);
  HashFloats(hash, &light, 1);
  hash ^= probes_.light_hash;
  // The still geometry, in any order: where each piece is, its size and color.
  uint64_t sum = 0;
  for (const FrameDraw* draw : still_) {
    uint64_t piece = kHashSeed;
    HashFloats(piece, &draw->bounds_min.x, 3);
    HashFloats(piece, &draw->bounds_max.x, 3);
    const DrawData& record = records_[draw->record];
    HashFloats(piece, &record.color.x, 8);  // color and emission
    const float indices = static_cast<float>(draw->index_count);
    HashFloats(piece, &indices, 1);
    sum += piece;
  }
  const float counts[2] = {static_cast<float>(still_.size()),
                           static_cast<float>(sum % 16777216u)};
  HashFloats(hash, counts, 2);
  return hash ^ (sum * 0x9e3779b97f4a7c15ull);
}

bool SceneRenderer::LoadProbeCache(VkCommandBuffer cmd, FrameResources& frame, uint64_t key,
                                   uint32_t total) {
  if (!ProbeCacheEnabled()) return false;
  const std::filesystem::path path = ProbeCacheFile(key);
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) return false;
  std::ifstream file(path, std::ios::binary);
  char magic[4] = {};
  uint64_t stored_key = 0;
  uint32_t stored_total = 0;
  uint64_t coefficient_bytes = 0, visibility_bytes = 0;
  file.read(magic, 4);
  file.read(reinterpret_cast<char*>(&stored_key), sizeof(stored_key));
  file.read(reinterpret_cast<char*>(&stored_total), sizeof(stored_total));
  file.read(reinterpret_cast<char*>(&coefficient_bytes), sizeof(coefficient_bytes));
  file.read(reinterpret_cast<char*>(&visibility_bytes), sizeof(visibility_bytes));
  const uint32_t rows = (total + kProbesPerAtlasRow - 1) / kProbesPerAtlasRow;
  const VkDeviceSize want_coefficients = VkDeviceSize{kProbesPerAtlasRow} * 9 * rows * 8;
  const VkDeviceSize want_visibility =
      VkDeviceSize{kProbesPerAtlasRow} * kProbeVisibilityTexels * rows * kProbeVisibilityTexels * 8;
  if (!file || std::memcmp(magic, kProbeCacheMagic, 4) != 0 || stored_key != key ||
      stored_total != total || coefficient_bytes != want_coefficients ||
      visibility_bytes != want_visibility) {
    return false;
  }
  Buffer staging;
  if (!staging.Create(context_->allocator, coefficient_bytes + visibility_bytes,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Buffer::Memory::kHostVisible)) {
    return false;
  }
  file.read(static_cast<char*>(staging.Mapped()),
            static_cast<std::streamsize>(coefficient_bytes + visibility_bytes));
  if (!file) return false;
  staging.Flush(0, staging.Size());

  for (VkImage image : {probe_atlas_.Get(), probe_visibility_.Get()}) {
    TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
  }
  VkBufferImageCopy coefficients = {};
  coefficients.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  coefficients.imageExtent = {kProbesPerAtlasRow * 9, rows, 1};
  vkCmdCopyBufferToImage(cmd, staging.Get(), probe_atlas_.Get(),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &coefficients);
  VkBufferImageCopy visibility = {};
  visibility.bufferOffset = coefficient_bytes;
  visibility.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  visibility.imageExtent = {kProbesPerAtlasRow * kProbeVisibilityTexels,
                            rows * kProbeVisibilityTexels, 1};
  vkCmdCopyBufferToImage(cmd, staging.Get(), probe_visibility_.Get(),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &visibility);
  for (VkImage image : {probe_atlas_.Get(), probe_visibility_.Get()}) {
    TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  frame.staging.push_back(std::move(staging));
  BuildProbeMaps(cmd, 0, total);
  return true;
}

void SceneRenderer::SaveProbeCache(VkCommandBuffer cmd, uint64_t key, uint32_t total) {
  if (!ProbeCacheEnabled() || key == 0 || probe_save_) return;
  const std::filesystem::path path = ProbeCacheFile(key);
  std::error_code ec;
  if (path.empty() || std::filesystem::exists(path, ec)) return;
  const uint32_t rows = (total + kProbesPerAtlasRow - 1) / kProbesPerAtlasRow;
  ProbeSave save;
  save.frame = frame_number_;
  save.key = key;
  save.total = total;
  save.coefficient_bytes = VkDeviceSize{kProbesPerAtlasRow} * 9 * rows * 8;
  const VkDeviceSize visibility_bytes =
      VkDeviceSize{kProbesPerAtlasRow} * kProbeVisibilityTexels * rows * kProbeVisibilityTexels * 8;
  if (!save.buffer.Create(context_->allocator, save.coefficient_bytes + visibility_bytes,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT, Buffer::Memory::kReadback)) {
    return;
  }
  for (VkImage image : {probe_atlas_.Get(), probe_visibility_.Get()}) {
    TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
  }
  VkBufferImageCopy coefficients = {};
  coefficients.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  coefficients.imageExtent = {kProbesPerAtlasRow * 9, rows, 1};
  vkCmdCopyImageToBuffer(cmd, probe_atlas_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         save.buffer.Get(), 1, &coefficients);
  VkBufferImageCopy visibility = {};
  visibility.bufferOffset = save.coefficient_bytes;
  visibility.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  visibility.imageExtent = {kProbesPerAtlasRow * kProbeVisibilityTexels,
                            rows * kProbeVisibilityTexels, 1};
  vkCmdCopyImageToBuffer(cmd, probe_visibility_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         save.buffer.Get(), 1, &visibility);
  for (VkImage image : {probe_atlas_.Get(), probe_visibility_.Get()}) {
    TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }
  probe_save_ = std::move(save);
}

void SceneRenderer::FinishProbeSave() {
  // Every frame slot has come around since the copy: it is done.
  if (!probe_save_ || probe_save_->frame + frames_in_flight_ > frame_number_) return;
  const std::filesystem::path path = ProbeCacheFile(probe_save_->key);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  const std::filesystem::path temporary = path.string() + ".tmp";
  probe_save_->buffer.Invalidate();
  {
    std::ofstream file(temporary, std::ios::binary);
    const uint64_t visibility_bytes = probe_save_->buffer.Size() - probe_save_->coefficient_bytes;
    const uint64_t coefficient_bytes = probe_save_->coefficient_bytes;
    file.write(kProbeCacheMagic, 4);
    file.write(reinterpret_cast<const char*>(&probe_save_->key), sizeof(uint64_t));
    file.write(reinterpret_cast<const char*>(&probe_save_->total), sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&coefficient_bytes), sizeof(uint64_t));
    file.write(reinterpret_cast<const char*>(&visibility_bytes), sizeof(uint64_t));
    file.write(static_cast<const char*>(probe_save_->buffer.Mapped()),
               static_cast<std::streamsize>(probe_save_->buffer.Size()));
    if (!file) PDebug::warn("Could not write the light probe cache {}", path.string());
  }
  std::filesystem::rename(temporary, path, ec);
  if (ec) std::filesystem::remove(temporary, ec);
  probe_save_.reset();
}

void SceneRenderer::DrawList(VkCommandBuffer cmd, FrameResources& frame, uint32_t uniform_offset,
                             const std::vector<const FrameDraw*>& list, bool transparent_pass) {
  if (list.empty()) return;
  const VkDescriptorSet sets[2] = {frame.set, texture_set_};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model_layout_, 0, 2, sets, 1,
                          &uniform_offset);
  const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
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
    vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
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
  // Opaque draws are culled and drawn on the GPU; transparent ones are
  // sorted here.
  const CulledView culled = CullView(cmd, frame, uniforms.view_proj, kCullAlive | kCullTransparent,
                                     kCullAlive, camera.culling_mask, {});

  ToAttachment(cmd, targets.hdr.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ToAttachment(cmd, targets.normal.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  DepthToAttachment(cmd, targets.depth.Get());
  // It pays where shading costs more than drawing the geometry twice:
  // lights besides the sun (each pixel lights by all of them), or a scene of
  // few triangles for its pixels.
  const bool heavy_shading =
      !lighting_.point_lights.empty() || !lighting_.area_lights.empty() ||
      live_triangles_ < 2ull * extent.width * extent.height;
  const bool prepass = (depth_prepass_ == 1 || (depth_prepass_ < 0 && heavy_shading)) &&
                       culled.valid && clear_depth;
  if (prepass) {
    GpuScope scope(gpu_timer_, cmd, "depth_prepass");
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = targets.depth.View();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    SetViewport(cmd, extent);
    DrawCulled(cmd, frame, culled, offset, prepass_[0], prepass_[1]);
    vkCmdEndRendering(cmd);
    GlobalBarrier(cmd, kDepthStages, kDepthAccess, kDepthStages, kDepthAccess);
  }

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
  depth.loadOp = clear_depth && !prepass ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
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
    if (!draw.alive || !(camera.culling_mask & (1u << (draw.layer & 31)))) continue;
    if (draw.transparent) {
      transparent.push_back(&draw);
    } else if (!culled.valid) {
      opaque.push_back(&draw);
    }
  }
  if (culled.valid) {
    DrawCulled(cmd, frame, culled, offset, opaque_[0], opaque_[1]);
  } else {
    std::stable_sort(opaque.begin(), opaque.end(), [](const FrameDraw* a, const FrameDraw* b) {
      return a->double_sided < b->double_sided;
    });
    DrawList(cmd, frame, offset, opaque, false);
  }
  std::sort(transparent.begin(), transparent.end(), [&](const FrameDraw* a, const FrameDraw* b) {
    const glm::vec3 da = a->center - camera.position, db = b->center - camera.position;
    return glm::dot(da, da) > glm::dot(db, db);
  });
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
    if (draw.alive && (draw.rendering_layers & settings.outline_layers) != 0 &&
        (camera.culling_mask & (1u << (draw.layer & 31)))) {
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
    const VkBuffer buffers[2] = {vertex_buffer_.Get(), instance_buffer_.Get()};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer_.Get(), 0, VK_INDEX_TYPE_UINT32);
    FrontFace front_face;
    for (const FrameDraw* draw : outlined) {
      front_face.Set(cmd, draw->mirrored);
      vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->first_index,
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
  {
    GpuScope scope(gpu_timer_, cmd, "main");
    DrawCamera(cmd, frame, *main, targets, true, true, false, post, &legacy, sky_texture,
               sky_texture != VK_NULL_HANDLE && clears);
  }

  if (main->post_processing && settings_.ssao_intensity > 0.0f) {
    GpuScope scope(gpu_timer_, cmd, "ssao");
    PostPush push;
    const glm::mat4& p = main->projection;
    push.p[0] = glm::vec4(1.0f / p[0][0], 1.0f / p[1][1], p[2][2], p[3][2]);
    push.p[1] = glm::vec4(settings_.ssao_radius, settings_.ssao_intensity,
                          static_cast<float>(settings_.ssao_samples), 0.0f);
    push.p[2] = glm::vec4(p[0][0], p[1][1], 0.0f, 0.0f);
    const VkExtent2D half = targets.ao.Extent();
    ToAttachment(cmd, targets.ao.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Fullscreen(cmd, ssao_, targets.ssao_set, targets.ao.View(), half, &push, false);
    ToShaderRead(cmd, targets.ao.Get());
    PostPush blur;
    blur.p[0] = glm::vec4(1.0f / half.width, 1.0f / half.height, 0.0f, 0.0f);
    ToAttachment(cmd, targets.ao_blur.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Fullscreen(cmd, blur_, targets.blur_set, targets.ao_blur.View(), half, &blur, false);
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
  GpuScope scope(gpu_timer_, cmd, "post");
  PostProcess(cmd, targets, *main, settings_, output, output_view, output_format, output_layout,
              false);
}

}  // namespace Mercury::vk
