#include "vulkan_application.h"

#include <GLFW/glfw3.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <stb/stb_image_write.h>

#include "components/components.h"
#include "debugger/debugger.h"
#include "glm/ext/matrix_transform.hpp"
#include "os/filesystem.h"
#include "properties/mesh.h"
#include "properties/mesh_instance.h"
#include "properties/texture2d.h"
#include "properties/transform.h"
#include "vulkan/vk_check.h"

namespace Mercury {

namespace {

// Descriptor bindings; must match the register shifts in CMakeLists.txt and
// the mapping documented at the top of each shader.
constexpr uint32_t kCameraBinding = 0;   // b0
constexpr uint32_t kAtlasBinding = 10;   // t0
constexpr uint32_t kSamplerBinding = 20; // s0

constexpr VkClearColorValue kClearColor = {{0.1f, 0.1f, 0.1f, 1.0f}};

void CheckImGuiVkResult(VkResult result) {
  if (result != VK_SUCCESS) {
    PDebug::error("ImGui Vulkan backend: {}", vk::ResultToString(result));
  }
}

}  // namespace

VulkanApplication::VulkanApplication(const std::string &window_title,
                                     float initial_width, float initial_height)
    : Application(window_title, initial_width, initial_height) {}

VulkanApplication::~VulkanApplication() {
  // Runs before Application::~Application destroys the GLFW window, so the
  // surface and swapchain are released while their window still exists.
  VkDevice device = context_.device;
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);

    if (imgui_vulkan_initialized_) ImGui_ImplVulkan_Shutdown();

    for (FrameData &frame : frames_) {
      frame.camera_ubo.Reset();
      frame.vertices.Reset();
      frame.indices.Reset();
      frame.static_vertices.Reset();
      frame.static_indices.Reset();
      frame.instance_staging.Reset();
      frame.atlas_staging.Reset();
      vkDestroyFence(device, frame.in_flight, nullptr);
      vkDestroySemaphore(device, frame.image_available, nullptr);
      vkDestroyCommandPool(device, frame.command_pool, nullptr);
    }
    instance_buffer_.Reset();
    atlas_image_.Reset();
    DestroySceneTarget();
    capture_buffer_.Reset();
    vkDestroySampler(device, scene_sampler_, nullptr);

    vkDestroySampler(device, texture_sampler_, nullptr);
    vkDestroyPipeline(device, instanced_pipeline_, nullptr);
    vkDestroyPipeline(device, static_pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout_, nullptr);
    swapchain_.Destroy();
  }

  if (imgui_glfw_initialized_) ImGui_ImplGlfw_Shutdown();
  if (ImGui::GetCurrentContext() != nullptr) ImGui::DestroyContext();

  context_.Destroy();
}

void VulkanApplication::ImGuiNewFrame() { ImGui_ImplVulkan_NewFrame(); }

bool VulkanApplication::Initialize() {
  if (!Application::Initialize()) {
    PDebug::error("Could not initialize GLFW application");
    return false;
  }

  if (!context_.Init(GetWindow())) {
    PDebug::error("Vulkan: failed to create device");
    return false;
  }

  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(GetWindow(), &framebuffer_width, &framebuffer_height);
  if (!swapchain_.Create(context_, static_cast<uint32_t>(framebuffer_width),
                         static_cast<uint32_t>(framebuffer_height))) {
    PDebug::error("Vulkan: could not create swapchain");
    return false;
  }

  if (!CreateFrameResources()) return false;
  if (!InitImGui()) return false;
  ReadCaptureSettings();
  return true;
}

bool VulkanApplication::CreateFrameResources() {
  VkDevice device = context_.device;
  VmaAllocator allocator = context_.allocator;
  using Memory = vk::Buffer::Memory;

  for (FrameData &frame : frames_) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = context_.queue_family;
    if (!VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr,
                                      &frame.command_pool))) {
      return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc_info.commandPool = frame.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    if (!VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info,
                                           &frame.command_buffer))) {
      return false;
    }

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {.sType =
                                        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr,
                                    &frame.image_available)) ||
        !VK_CHECK(
            vkCreateFence(device, &fence_info, nullptr, &frame.in_flight))) {
      return false;
    }

    const bool buffers_ok =
        frame.camera_ubo.Create(allocator, sizeof(CameraCBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                Memory::kHostVisible) &&
        frame.vertices.Create(allocator, sizeof(Vertex) * kMaxVertices,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              Memory::kHostVisible) &&
        frame.indices.Create(allocator, sizeof(UINT) * kMaxIndices,
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             Memory::kHostVisible) &&
        frame.static_vertices.Create(
            allocator, sizeof(StaticVertex) * kMaxStaticVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Memory::kHostVisible) &&
        frame.static_indices.Create(allocator, sizeof(UINT) * kMaxIndices,
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    Memory::kHostVisible) &&
        frame.instance_staging.Create(allocator, kInstanceBufferBytes,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      Memory::kHostVisible);
    if (!buffers_ok) {
      PDebug::error("Vulkan: failed to create per-frame buffers");
      return false;
    }
  }

  // Persistent per-instance buffer, bound on vertex binding 1. Dirty slots
  // are patched with transfer copies across frames instead of rebuilding it,
  // so it must be sized for the worst-case live instance count.
  if (!instance_buffer_.Create(
          allocator, kInstanceBufferBytes,
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          Memory::kDeviceLocal)) {
    PDebug::error("Vulkan: failed to create instance buffer");
    return false;
  }
  return true;
}

bool VulkanApplication::InitImGui() {
  if (!ImGui_ImplGlfw_InitForVulkan(GetWindow(), true)) {
    PDebug::error("ImGui: GLFW backend init failed");
    return false;
  }
  imgui_glfw_initialized_ = true;

  const VkFormat color_format = swapchain_.ColorFormat();
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.ApiVersion = vk::Context::kApiVersion;
  init_info.Instance = context_.instance;
  init_info.PhysicalDevice = context_.physical_device;
  init_info.Device = context_.device;
  init_info.QueueFamily = context_.queue_family;
  init_info.Queue = context_.queue;
  // Room for the font atlas plus user textures such as the editor's scene
  // view.
  init_info.DescriptorPoolSize =
      IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 16;
  init_info.MinImageCount = swapchain_.MinImageCount();
  init_info.ImageCount = swapchain_.ImageCount();
  init_info.UseDynamicRendering = true;
  // ImGui draws inside the same rendering pass as the scene, so its pipeline
  // must declare the same color and depth attachment formats.
  VkPipelineRenderingCreateInfoKHR &rendering_info =
      init_info.PipelineInfoMain.PipelineRenderingCreateInfo;
  rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &color_format;
  rendering_info.depthAttachmentFormat = vk::Swapchain::kDepthFormat;
  init_info.CheckVkResultFn = CheckImGuiVkResult;

  if (!ImGui_ImplVulkan_Init(&init_info)) {
    PDebug::error("ImGui: Vulkan backend init failed");
    return false;
  }
  imgui_vulkan_initialized_ = true;
  return true;
}

void VulkanApplication::OnNewTick() {
  vertex_buffer_.clear();
  index_buffer_.clear();

  if (!game_scene_) return;

  auto &registry = game_scene_->GetRegistry();
  // Iterate every Mesh — a Mesh without a Transform is a geometry template
  // referenced by MeshInstance entities and still needs its vertices/indices
  // in the GPU buffers. The Render() pass uses the same view in the same
  // order so per-mesh start_index lines up exactly.
  auto mesh_view = registry.view<Mesh>();
  for (auto [entity, mesh] : mesh_view.each()) {
    const auto offset = static_cast<UINT>(vertex_buffer_.size());
    for (UINT index : mesh.indices_) {
      index_buffer_.push_back(index + offset);
    }
    vertex_buffer_.insert(vertex_buffer_.end(), mesh.vertices_.begin(),
                          mesh.vertices_.end());
  }
}

VkShaderModule VulkanApplication::LoadShaderModule(
    const std::string &file_name) const {
  const fs::path path = fs::path(MERCURY_SPIRV_PATH) / file_name;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    PDebug::error("Vulkan: could not open shader {}", path.string());
    return VK_NULL_HANDLE;
  }
  const auto size = static_cast<size_t>(file.tellg());
  if (size == 0 || size % sizeof(uint32_t) != 0) {
    PDebug::error("Vulkan: {} is not valid SPIR-V", path.string());
    return VK_NULL_HANDLE;
  }
  std::vector<uint32_t> code(size / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(code.data()),
            static_cast<std::streamsize>(size));

  VkShaderModuleCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  create_info.codeSize = size;
  create_info.pCode = code.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (!VK_CHECK(vkCreateShaderModule(context_.device, &create_info, nullptr,
                                     &module))) {
    return VK_NULL_HANDLE;
  }
  return module;
}

template <typename F>
bool VulkanApplication::SubmitImmediate(F &&record) {
  VkDevice device = context_.device;
  VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc_info.commandPool = frames_[0].command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (!VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &cmd))) {
    return false;
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin_info);
  record(cmd);
  vkEndCommandBuffer(cmd);

  VkCommandBufferSubmitInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmd_info.commandBuffer = cmd;
  VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cmd_info;
  const bool ok =
      VK_CHECK(vkQueueSubmit2(context_.queue, 1, &submit, VK_NULL_HANDLE)) &&
      VK_CHECK(vkQueueWaitIdle(context_.queue));
  vkFreeCommandBuffers(device, frames_[0].command_pool, 1, &cmd);
  return ok;
}

bool VulkanApplication::CreateDescriptors() {
  VkDevice device = context_.device;

  const VkDescriptorSetLayoutBinding bindings[] = {
      {kCameraBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {kAtlasBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {kSamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
  };
  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_info.bindingCount = static_cast<uint32_t>(std::size(bindings));
  layout_info.pBindings = bindings;
  if (!VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                            &descriptor_set_layout_))) {
    return false;
  }

  const VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kFramesInFlight},
      {VK_DESCRIPTOR_TYPE_SAMPLER, kFramesInFlight},
  };
  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = kFramesInFlight;
  pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
  pool_info.pPoolSizes = pool_sizes;
  if (!VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr,
                                       &descriptor_pool_))) {
    return false;
  }

  std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
  layouts.fill(descriptor_set_layout_);
  std::array<VkDescriptorSet, kFramesInFlight> sets = {};
  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc_info.descriptorPool = descriptor_pool_;
  alloc_info.descriptorSetCount = kFramesInFlight;
  alloc_info.pSetLayouts = layouts.data();
  if (!VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, sets.data()))) {
    return false;
  }
  for (uint32_t i = 0; i < kFramesInFlight; ++i) {
    frames_[i].descriptor_set = sets[i];
  }

  VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
  return VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info,
                                         nullptr, &pipeline_layout_));
}

void VulkanApplication::WriteDescriptorSets() {
  for (FrameData &frame : frames_) {
    VkDescriptorBufferInfo camera_info = {frame.camera_ubo.Get(), 0,
                                          sizeof(CameraCBuffer)};
    VkDescriptorImageInfo atlas_info = {
        VK_NULL_HANDLE, atlas_image_.View(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo sampler_info = {texture_sampler_, VK_NULL_HANDLE,
                                          VK_IMAGE_LAYOUT_UNDEFINED};

    VkWriteDescriptorSet writes[3] = {};
    for (auto &write : writes) {
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = frame.descriptor_set;
      write.descriptorCount = 1;
    }
    writes[0].dstBinding = kCameraBinding;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &camera_info;
    writes[1].dstBinding = kAtlasBinding;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].pImageInfo = &atlas_info;
    writes[2].dstBinding = kSamplerBinding;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[2].pImageInfo = &sampler_info;
    vkUpdateDescriptorSets(context_.device, 3, writes, 0, nullptr);
  }
}

bool VulkanApplication::CreatePipelines() {
  VkDevice device = context_.device;

  VkShaderModule main_vs = LoadShaderModule("main.vs.spv");
  VkShaderModule static_vs = LoadShaderModule("static.vs.spv");
  VkShaderModule textured_ps = LoadShaderModule("textured.ps.spv");
  auto destroy_modules = [&] {
    vkDestroyShaderModule(device, main_vs, nullptr);
    vkDestroyShaderModule(device, static_vs, nullptr);
    vkDestroyShaderModule(device, textured_ps, nullptr);
  };
  if (!main_vs || !static_vs || !textured_ps) {
    destroy_modules();
    return false;
  }

  // Fixed-function state shared by both pipelines; mirrors the old D3D11
  // blend/rasterizer/depth-stencil state objects.
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // The negative-height viewport keeps the D3D-style orientation, so the
  // winding convention carries over unchanged.
  VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.blendEnable = VK_TRUE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                           VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  const VkFormat color_format = swapchain_.ColorFormat();
  VkPipelineRenderingCreateInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachmentFormats = &color_format;
  rendering_info.depthAttachmentFormat = vk::Swapchain::kDepthFormat;

  auto build = [&](VkShaderModule vs,
                   const VkPipelineVertexInputStateCreateInfo &vertex_input,
                   VkPipeline &out) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "Main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = textured_ps;
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
    return VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                              &create_info, nullptr, &out));
  };

  // Instanced pipeline: binding 0 per-vertex (Vertex), binding 1
  // per-instance (VsModelBuffer). Locations match main.vs.hlsl.
  const VkVertexInputBindingDescription instanced_bindings[] = {
      {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(VsModelBuffer), VK_VERTEX_INPUT_RATE_INSTANCE},
  };
  const VkVertexInputAttributeDescription instanced_attributes[] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
      {10, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
      {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VsModelBuffer, scale)},
      {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(VsModelBuffer, opacity)},
      {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VsModelBuffer, world_)},
      {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
       offsetof(VsModelBuffer, world_) + 16},
      {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
       offsetof(VsModelBuffer, world_) + 32},
      {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
       offsetof(VsModelBuffer, world_) + 48},
      {9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VsModelBuffer, atlas_uv)},
  };
  VkPipelineVertexInputStateCreateInfo instanced_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  instanced_input.vertexBindingDescriptionCount =
      static_cast<uint32_t>(std::size(instanced_bindings));
  instanced_input.pVertexBindingDescriptions = instanced_bindings;
  instanced_input.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(std::size(instanced_attributes));
  instanced_input.pVertexAttributeDescriptions = instanced_attributes;

  // Static pipeline: pre-transformed StaticVertex. Locations match
  // static.vs.hlsl.
  const VkVertexInputBindingDescription static_binding = {
      0, sizeof(StaticVertex), VK_VERTEX_INPUT_RATE_VERTEX};
  const VkVertexInputAttributeDescription static_attributes[] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticVertex, position)},
      {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(StaticVertex, color)},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticVertex, uv)},
      {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(StaticVertex, atlas_uv)},
  };
  VkPipelineVertexInputStateCreateInfo static_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  static_input.vertexBindingDescriptionCount = 1;
  static_input.pVertexBindingDescriptions = &static_binding;
  static_input.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(std::size(static_attributes));
  static_input.pVertexAttributeDescriptions = static_attributes;

  const bool ok = build(main_vs, instanced_input, instanced_pipeline_) &&
                  build(static_vs, static_input, static_pipeline_);
  destroy_modules();
  return ok;
}

bool VulkanApplication::CreateAtlasImage(uint32_t width, uint32_t height) {
  if (!atlas_image_.Create(
          context_.allocator, context_.device, width, height,
          VK_FORMAT_R8G8B8A8_UNORM,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT)) {
    PDebug::error("Vulkan: failed to create {}x{} atlas image", width, height);
    return false;
  }

  // Give the image defined contents and the layout the descriptors expect,
  // so sampling before the first atlas upload is valid.
  VkImage image = atlas_image_.Get();
  const bool ok = SubmitImmediate([&](VkCommandBuffer cmd) {
    vk::TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, 0,
                        VK_PIPELINE_STAGE_2_CLEAR_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue transparent = {{0.0f, 0.0f, 0.0f, 0.0f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                           1};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &transparent, 1, &range);
    vk::TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_CLEAR_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  });
  atlas_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  return ok;
}

bool VulkanApplication::OnLoad() {
  if (!game_scene_) {
    PDebug::error("Vulkan: no game scene set before load");
    return false;
  }

  if (!CreateDescriptors()) {
    PDebug::error("Vulkan: failed to create descriptor sets");
    return false;
  }
  if (!CreatePipelines()) {
    PDebug::error("Vulkan: failed to create pipelines");
    return false;
  }

  VkSamplerCreateInfo sampler_info = {.sType =
                                          VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = VK_LOD_CLAMP_NONE;
  if (!VK_CHECK(vkCreateSampler(context_.device, &sampler_info, nullptr,
                                &texture_sampler_))) {
    PDebug::error("Vulkan: failed to create texture sampler");
    return false;
  }

  // Single atlas texture; uploaded only when the atlas changes.
  TextureAtlas &atlas = game_scene_->GetAtlas();
  if (!CreateAtlasImage(static_cast<uint32_t>(atlas.GetWidth()),
                        static_cast<uint32_t>(atlas.GetHeight()))) {
    return false;
  }

  WriteDescriptorSets();
  return true;
}

VsModelBuffer VulkanApplication::BuildInstanceRecord(
    entt::registry &registry, TextureAtlas &atlas, entt::entity entity,
    entt::entity parent_mesh_entity) {
  VsModelBuffer record = {};
  auto *transform = registry.try_get<Transform>(entity);
  if (!transform) return record;

  record.scale = transform->GetInterpolatedScale();
  auto position = transform->GetInterpolatedPosition();
  glm::mat4 model(1.0);
  model = glm::translate(model, position);
  model = model * transform->GetRotationMatrix();
  record.world_ = model;

  float opacity = 1.0f;
  if (auto *parent_mesh = registry.try_get<Mesh>(parent_mesh_entity)) {
    opacity = parent_mesh->opacity_;
  }
  record.opacity = opacity;

  glm::vec4 atlas_uv = atlas.GetWhitePixelUV();
  if (auto *tex = registry.try_get<Texture2D>(entity)) {
    atlas_uv = tex->GetAtlasUV();
  } else if (auto *ref = registry.try_get<TextureRef>(entity);
             ref && ref->texture) {
    atlas_uv = ref->texture->GetAtlasUV();
  } else if (entity != parent_mesh_entity) {
    if (auto *parent_tex = registry.try_get<Texture2D>(parent_mesh_entity)) {
      atlas_uv = parent_tex->GetAtlasUV();
    }
  }
  record.atlas_uv = atlas_uv;
  return record;
}

void VulkanApplication::EnsureRegistrySignalsHooked(entt::registry &registry) {
  if (hooked_registry_ == &registry) return;
  // (No need to detach from the old registry — when scenes are swapped the
  // whole VulkanApplication persists but the registry is owned by the scene
  // and is destroyed with it, taking its sigh slots with it.)
  registry.on_construct<Mesh>().connect<&VulkanApplication::OnLayoutChanged>(
      *this);
  registry.on_destroy<Mesh>().connect<&VulkanApplication::OnLayoutChanged>(
      *this);
  registry.on_construct<MeshInstance>()
      .connect<&VulkanApplication::OnLayoutChanged>(*this);
  registry.on_destroy<MeshInstance>()
      .connect<&VulkanApplication::OnLayoutChanged>(*this);
  registry.on_construct<Transform>()
      .connect<&VulkanApplication::OnLayoutChanged>(*this);
  registry.on_destroy<Transform>().connect<&VulkanApplication::OnLayoutChanged>(
      *this);
  hooked_registry_ = &registry;
  layout_dirty_ = true;
}

void VulkanApplication::RebuildInstanceLayout(entt::registry &registry,
                                              TextureAtlas &atlas,
                                              FrameData &frame) {
  mesh_layout_.clear();
  mesh_order_.clear();
  entity_slot_.clear();
  slot_owner_.clear();
  slot_parent_.clear();

  std::vector<VsModelBuffer> instance_data;
  instance_data.reserve(64);

  UINT running_index = 0;
  auto mesh_view = registry.view<Mesh>();
  for (auto [entity, mesh] : mesh_view.each()) {
    MeshLayoutEntry entry;
    entry.index_count = static_cast<UINT>(mesh.indices_.size());
    entry.start_index = running_index;
    entry.start_instance = static_cast<UINT>(instance_data.size());
    entry.instance_count = 0;

    if (registry.try_get<Transform>(entity)) {
      UINT slot = static_cast<UINT>(instance_data.size());
      instance_data.push_back(
          BuildInstanceRecord(registry, atlas, entity, entity));
      entity_slot_.emplace(entity, slot);
      slot_owner_.push_back(entity);
      slot_parent_.push_back(entity);
      entry.instance_count = 1;
    }

    mesh_layout_.emplace(entity, entry);
    mesh_order_.push_back(entity);
    running_index += entry.index_count;
  }

  auto instance_view = registry.view<MeshInstance, Transform>();
  for (auto [entity, mesh_instance, transform] : instance_view.each()) {
    auto parent = mesh_instance.GetParentEntity();
    auto layout_it = mesh_layout_.find(parent);
    if (layout_it == mesh_layout_.end()) continue;
    if (!registry.try_get<Mesh>(parent)) continue;

    // Append at end of this mesh's slice. Because we visit all MeshInstances
    // in one pass, we must shift every later mesh's start_instance up by one
    // and shift the actual instance_data accordingly.
    UINT insert_slot =
        layout_it->second.start_instance + layout_it->second.instance_count;
    instance_data.insert(instance_data.begin() + insert_slot,
                         BuildInstanceRecord(registry, atlas, entity, parent));
    slot_owner_.insert(slot_owner_.begin() + insert_slot, entity);
    slot_parent_.insert(slot_parent_.begin() + insert_slot, parent);
    layout_it->second.instance_count += 1;

    // Bump start_instance for every mesh whose slice comes after this one,
    // and re-index entity_slot_ for entities whose slot >= insert_slot.
    for (auto &kv : mesh_layout_) {
      if (kv.first != parent && kv.second.start_instance >= insert_slot) {
        kv.second.start_instance += 1;
      }
    }
    for (auto &kv : entity_slot_) {
      if (kv.second >= insert_slot) kv.second += 1;
    }
    entity_slot_.emplace(entity, insert_slot);
  }

  if (instance_data.size() > kMaxInstances) {
    PDebug::error(
        "Vulkan: instance buffer overflow ({} > {}); truncating. Raise "
        "kMaxInstances.",
        instance_data.size(), kMaxInstances);
    instance_data.resize(kMaxInstances);
  }

  if (!instance_data.empty()) {
    const VkDeviceSize bytes = sizeof(VsModelBuffer) * instance_data.size();
    frame.instance_staging.Write(instance_data.data(), bytes);
    instance_copies_.push_back({0, 0, bytes});
  }

  // Stamp every transform we wrote as clean so the next-frame fast path
  // doesn't re-patch what we just uploaded.
  auto transform_view = registry.view<Transform>();
  for (auto [entity, transform] : transform_view.each()) {
    transform.ClearDirty();
  }
  auto texture_view = registry.view<Texture2D>();
  for (auto [entity, tex] : texture_view.each()) {
    tex.ClearDirty();
  }

  layout_dirty_ = false;
}

void VulkanApplication::UpdateDirtyInstances(entt::registry &registry,
                                             TextureAtlas &atlas,
                                             FrameData &frame) {
  // An entity is in entity_slot_ only if it owns a live slot in the instance
  // buffer; Transforms without Mesh/MeshInstance attachment have no slot to
  // patch and are silently skipped.
  auto transform_view = registry.view<Transform>();
  for (auto [entity, transform] : transform_view.each()) {
    bool dirty = transform.IsDirty();
    if (!dirty) {
      if (auto *tex = registry.try_get<Texture2D>(entity)) {
        dirty = tex->IsDirty();
      }
    }
    if (!dirty) continue;
    auto slot_it = entity_slot_.find(entity);
    if (slot_it == entity_slot_.end() || slot_it->second >= kMaxInstances) {
      transform.ClearDirty();
      continue;
    }
    const UINT slot = slot_it->second;
    entt::entity parent = slot_parent_[slot];
    const VsModelBuffer record =
        BuildInstanceRecord(registry, atlas, entity, parent);
    const VkDeviceSize offset = sizeof(VsModelBuffer) * slot;
    frame.instance_staging.Write(&record, sizeof(record), offset);
    instance_copies_.push_back({offset, offset, sizeof(record)});
    transform.ClearDirty();
    if (auto *tex = registry.try_get<Texture2D>(entity)) tex->ClearDirty();
  }
}

void VulkanApplication::RecordAtlasUpload(FrameData &frame,
                                          TextureAtlas &atlas) {
  const auto width = static_cast<uint32_t>(atlas.GetWidth());
  const auto height = static_cast<uint32_t>(atlas.GetHeight());
  const VkExtent2D extent = atlas_image_.Extent();
  if (extent.width != width || extent.height != height) {
    // The other frame in flight may still sample the old image.
    vkDeviceWaitIdle(context_.device);
    if (!CreateAtlasImage(width, height)) return;
    WriteDescriptorSets();
  }

  const VkDeviceSize bytes = VkDeviceSize{width} * height * 4;
  if (!frame.atlas_staging.Create(context_.allocator, bytes,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  vk::Buffer::Memory::kHostVisible) ||
      !frame.atlas_staging.Write(atlas.GetData(), bytes)) {
    PDebug::error("Vulkan: failed to stage atlas upload");
    return;
  }

  VkCommandBuffer cmd = frame.command_buffer;
  VkImage image = atlas_image_.Get();
  // Earlier frames' fragment shaders may still be sampling the atlas.
  vk::TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, atlas_layout_,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, frame.atlas_staging.Get(), image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  vk::TransitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  atlas_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  atlas.ClearGpuUploadFlag();
}

void VulkanApplication::RecordInstanceUploads(FrameData &frame) {
  if (instance_copies_.empty()) return;
  VkCommandBuffer cmd = frame.command_buffer;
  // Earlier submissions may still be reading the instance buffer as vertex
  // input; GPU queue ordering plus these barriers make a single device-local
  // copy safe with multiple frames in flight.
  vk::GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, 0,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
  vkCmdCopyBuffer(cmd, frame.instance_staging.Get(), instance_buffer_.Get(),
                  static_cast<uint32_t>(instance_copies_.size()),
                  instance_copies_.data());
  vk::GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                    VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
}

bool VulkanApplication::UploadToFrameBuffer(vk::Buffer &buffer,
                                            const void *data,
                                            VkDeviceSize bytes,
                                            const char *name) {
  if (buffer.Write(data, bytes)) return true;
  if (!overflow_logged_) {
    PDebug::error(
        "Vulkan: {} needs {} bytes but its buffer holds {}; skipping draw",
        name, bytes, buffer.Size());
    overflow_logged_ = true;
  }
  return false;
}

void VulkanApplication::Render() {
  if (swapchain_dirty_) {
    RecreateSwapchain();
    if (swapchain_dirty_) {  // still 0x0 or failed; try again next frame
      ImGui::EndFrame();
      return;
    }
  }

  FrameData &frame = frames_[frame_index_];
  VkDevice device = context_.device;
  vkWaitForFences(device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
  // The upload that used this staging buffer has finished.
  frame.atlas_staging.Reset();

  uint32_t image_index = 0;
  VkResult acquire =
      vkAcquireNextImageKHR(device, swapchain_.Get(), UINT64_MAX,
                            frame.image_available, VK_NULL_HANDLE, &image_index);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
    swapchain_dirty_ = true;
    ImGui::EndFrame();
    return;
  }
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
    VK_CHECK(acquire);
    ImGui::EndFrame();
    return;
  }
  if (acquire == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
  // Only reset once we know work will be submitted, or the next wait on this
  // fence would deadlock.
  vkResetFences(device, 1, &frame.in_flight);

  const VkExtent2D extent = swapchain_.Extent();
  if (game_scene_) {
    glm::mat4x4 camera_proj_view = {};
    if (game_scene_->GetCameraProj(camera_proj_view)) {
      camera_.mWorldViewProj = camera_proj_view;
      camera_.mResolution =
          PQ_FLOAT2{game_scene_->GetWidth(), game_scene_->GetHeight()};
    }
    const SceneLighting &lighting = game_scene_->GetLighting();
    camera_.mLightDirection = PQ_FLOAT4(lighting.direction, 0.0f);
    camera_.mLightColor = PQ_FLOAT4(lighting.color, 1.0f);
    camera_.mAmbientColor = PQ_FLOAT4(lighting.ambient, 1.0f);
    glm::vec3 camera_position(0.0f);
    game_scene_->GetCameraPosition(camera_position);
    camera_.mCameraPosition = PQ_FLOAT4(camera_position, 1.0f);
  }
  frame.camera_ubo.Write(&camera_, sizeof(camera_));

  vkResetCommandPool(device, frame.command_pool, 0);
  VkCommandBuffer cmd = frame.command_buffer;
  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin_info);

  int copies = 0;
  bool draw_static = false;
  UINT static_index_count = 0;
  bool draw_instanced = false;

  if (game_scene_) {
    // Re-upload the atlas only when a new image was added since the last
    // frame. UpdateAtlas() (called from GetPrimitives()) sets the flag; it is
    // cleared once the copy is recorded.
    TextureAtlas &atlas = game_scene_->GetAtlas();
    if (atlas.NeedsGpuUpload()) {
      RecordAtlasUpload(frame, atlas);
      // Repacking moves every image, and TextureRef users have no dirty flag
      // of their own, so re-stage every instance's atlas UV.
      layout_dirty_ = true;
    }

    auto static_vertices = game_scene_->GetStaticVertices();
    auto static_indices = game_scene_->GetStaticIndices();
    if (!static_vertices.empty()) {
      draw_static =
          UploadToFrameBuffer(frame.static_vertices, static_vertices.data(),
                              sizeof(StaticVertex) * static_vertices.size(),
                              "static vertices") &&
          UploadToFrameBuffer(frame.static_indices, static_indices.data(),
                              sizeof(UINT) * static_indices.size(),
                              "static indices");
      static_index_count = static_cast<UINT>(static_indices.size());
    }

    draw_instanced =
        UploadToFrameBuffer(frame.vertices, vertex_buffer_.data(),
                            sizeof(Vertex) * vertex_buffer_.size(),
                            "vertices") &&
        UploadToFrameBuffer(frame.indices, index_buffer_.data(),
                            sizeof(UINT) * index_buffer_.size(), "indices");

    // Instance pipeline: layout (which mesh owns which slice of which
    // instance buffer) is cached and only rebuilt when entities are added
    // or removed; otherwise each frame patches just the slots whose
    // Transform/Texture2D went dirty. See RebuildInstanceLayout /
    // UpdateDirtyInstances.
    auto &registry = game_scene_->GetRegistry();
    EnsureRegistrySignalsHooked(registry);

    instance_copies_.clear();
    if (layout_dirty_) {
      RebuildInstanceLayout(registry, atlas, frame);
      copies += 1;
    } else {
      UpdateDirtyInstances(registry, atlas, frame);
    }
    RecordInstanceUploads(frame);
  }

  constexpr VkPipelineStageFlags2 kDepthStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

  // Editor: draw the scene into the offscreen target first; the window pass
  // then only draws ImGui, which shows that target inside a panel.
  const bool offscreen =
      scene_texture_enabled_ && scene_color_.Get() != VK_NULL_HANDLE;
  if (offscreen) {
    const VkExtent2D scene_extent = scene_color_.Extent();
    // Earlier frames may still be sampling the target in their ImGui pass.
    vk::TransitionImage(cmd, scene_color_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    vk::TransitionImage(cmd, scene_depth_.Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        kDepthStages,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = scene_color_.View();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = SceneClearColor();
    VkRenderingAttachmentInfo depth = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = scene_depth_.View();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo scene_rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    scene_rendering.renderArea = {{0, 0}, scene_extent};
    scene_rendering.layerCount = 1;
    scene_rendering.colorAttachmentCount = 1;
    scene_rendering.pColorAttachments = &color;
    scene_rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &scene_rendering);
    RecordSceneDraws(cmd, frame, scene_extent, draw_static, static_index_count,
                     draw_instanced);
    vkCmdEndRendering(cmd);

    vk::TransitionImage(cmd, scene_color_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }

  const VkImage swapchain_image = swapchain_.ImageAt(image_index);
  vk::TransitionImage(cmd, swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  // One depth image is shared by all frames in flight, so wait for the
  // previous frame's depth writes before clearing it.
  vk::TransitionImage(cmd, swapchain_.Depth().Get(), VK_IMAGE_ASPECT_DEPTH_BIT,
                      VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      kDepthStages,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

  VkRenderingAttachmentInfo color_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color_attachment.imageView = swapchain_.ImageView(image_index);
  color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.clearValue.color =
      scene_texture_enabled_ ? kClearColor : SceneClearColor();

  VkRenderingAttachmentInfo depth_attachment = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth_attachment.imageView = swapchain_.Depth().View();
  depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.clearValue.depthStencil = {1.0f, 0};

  VkRenderingInfo rendering_info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering_info.renderArea = {{0, 0}, extent};
  rendering_info.layerCount = 1;
  rendering_info.colorAttachmentCount = 1;
  rendering_info.pColorAttachments = &color_attachment;
  rendering_info.pDepthAttachment = &depth_attachment;
  vkCmdBeginRendering(cmd, &rendering_info);

  if (!scene_texture_enabled_) {
    RecordSceneDraws(cmd, frame, extent, draw_static, static_index_count,
                     draw_instanced);
  }

  if (show_render_stats_) {
    ImGui::SetNextWindowBgAlpha(0.3);
    ImGui::SetNextWindowPos(ImVec2{10.0, 10.0});
    ImGui::Begin("Render Stats", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoDocking);
    ImGui::Text("FPS: %d", average_fps_);
    ImGui::Text("GPU Memory Copies: %d", copies);
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  vkCmdEndRendering(cmd);

  const bool capture_now = !capture_path_.empty() &&
                           frame_counter_ == capture_frame_ &&
                           capture_buffer_.Create(
                               context_.allocator,
                               VkDeviceSize{extent.width} * extent.height * 4,
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               vk::Buffer::Memory::kHostVisible);
  if (capture_now) {
    vk::TransitionImage(cmd, swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, swapchain_image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           capture_buffer_.Get(), 1, &region);
    capture_extent_ = extent;
    vk::TransitionImage(cmd, swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_NONE, 0);
  } else {
    vk::TransitionImage(cmd, swapchain_image, VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_NONE, 0);
  }
  vkEndCommandBuffer(cmd);

  VkSemaphoreSubmitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait_info.semaphore = frame.image_available;
  wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal_info.semaphore = swapchain_.RenderFinished(image_index);
  signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkCommandBufferSubmitInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cmd_info.commandBuffer = cmd;

  VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.waitSemaphoreInfoCount = 1;
  submit.pWaitSemaphoreInfos = &wait_info;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cmd_info;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal_info;
  VK_CHECK(vkQueueSubmit2(context_.queue, 1, &submit, frame.in_flight));

  VkSwapchainKHR swapchain = swapchain_.Get();
  VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &signal_info.semaphore;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain;
  present.pImageIndices = &image_index;
  const VkResult present_result = vkQueuePresentKHR(context_.queue, &present);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
      present_result == VK_SUBOPTIMAL_KHR) {
    swapchain_dirty_ = true;
  } else {
    VK_CHECK(present_result);
  }

  frame_index_ = (frame_index_ + 1) % kFramesInFlight;

  if (capture_now) WriteCapture();
  frame_counter_++;
  // Resizing between frames means this frame's ImGui draw data (which may
  // reference the old target) has already been submitted.
  if (scene_texture_enabled_ && scene_target_dirty_) RecreateSceneTarget();
}

VkClearColorValue VulkanApplication::SceneClearColor() const {
  if (!game_scene_) return kClearColor;
  const glm::vec4 c = game_scene_->GetClearColor();
  return {{c.r, c.g, c.b, c.a}};
}

void VulkanApplication::RecordSceneDraws(VkCommandBuffer cmd, FrameData &frame,
                                         VkExtent2D extent, bool draw_static,
                                         UINT static_index_count,
                                         bool draw_instanced) {
  // Negative height flips Y so clip space matches D3D (+Y up), letting the
  // projection matrices and triangle winding stay as they were.
  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = static_cast<float>(extent.height);
  viewport.width = static_cast<float>(extent.width);
  viewport.height = -static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  const VkRect2D scissor = {{0, 0}, extent};
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  if (draw_static) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, static_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &frame.descriptor_set, 0,
                            nullptr);
    const VkBuffer vertex_buffer = frame.static_vertices.Get();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd, frame.static_indices.Get(), 0,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, static_index_count, 1, 0, 0, 0);
  }

  if (draw_instanced && !mesh_order_.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      instanced_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, 0, 1, &frame.descriptor_set, 0,
                            nullptr);
    const VkBuffer vertex_buffers[2] = {frame.vertices.Get(),
                                        instance_buffer_.Get()};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, frame.indices.Get(), 0, VK_INDEX_TYPE_UINT32);

    for (auto entity : mesh_order_) {
      const auto &entry = mesh_layout_[entity];
      if (entry.instance_count == 0) continue;
      if (entry.start_instance >= kMaxInstances) continue;
      const UINT instance_count = std::min(
          entry.instance_count, kMaxInstances - entry.start_instance);
      vkCmdDrawIndexed(cmd, entry.index_count, instance_count,
                       entry.start_index, 0, entry.start_instance);
    }
  }
}

void VulkanApplication::EnableSceneTexture(bool enabled) {
  scene_texture_enabled_ = enabled;
  if (enabled) {
    scene_target_dirty_ = true;
  } else if (game_scene_) {
    game_scene_->SetWidth(static_cast<float>(GetWidth()));
    game_scene_->SetHeight(static_cast<float>(GetHeight()));
  }
}

void VulkanApplication::SetSceneViewportSize(int width, int height) {
  const uint32_t w = static_cast<uint32_t>(std::max(width, 1));
  const uint32_t h = static_cast<uint32_t>(std::max(height, 1));
  if (w == scene_requested_.width && h == scene_requested_.height) return;
  scene_requested_ = {w, h};
  scene_target_dirty_ = true;
}

uint64_t VulkanApplication::GetSceneTexture() const {
  return reinterpret_cast<uint64_t>(scene_texture_set_);
}

void VulkanApplication::DestroySceneTarget() {
  if (scene_texture_set_ != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_RemoveTexture(scene_texture_set_);
    scene_texture_set_ = VK_NULL_HANDLE;
  }
  scene_color_.Reset();
  scene_depth_.Reset();
}

void VulkanApplication::RecreateSceneTarget() {
  scene_target_dirty_ = false;
  if (scene_requested_.width == 0 || scene_requested_.height == 0) return;
  const VkExtent2D current = scene_color_.Extent();
  if (scene_color_.Get() != VK_NULL_HANDLE &&
      current.width == scene_requested_.width &&
      current.height == scene_requested_.height) {
    return;
  }

  vkDeviceWaitIdle(context_.device);
  DestroySceneTarget();

  if (scene_sampler_ == VK_NULL_HANDLE) {
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 1.0f;
    if (!VK_CHECK(vkCreateSampler(context_.device, &sampler_info, nullptr,
                                  &scene_sampler_))) {
      return;
    }
  }

  // Same formats as the window, so the scene pipelines work for both.
  const bool ok =
      scene_color_.Create(
          context_.allocator, context_.device, scene_requested_.width,
          scene_requested_.height, swapchain_.ColorFormat(),
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          VK_IMAGE_ASPECT_COLOR_BIT) &&
      scene_depth_.Create(context_.allocator, context_.device,
                          scene_requested_.width, scene_requested_.height,
                          vk::Swapchain::kDepthFormat,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
  if (!ok) {
    PDebug::error("Vulkan: could not create {}x{} scene target",
                  scene_requested_.width, scene_requested_.height);
    DestroySceneTarget();
    return;
  }

  // ImGui samples the image in SHADER_READ_ONLY layout; give it defined
  // contents in that layout before the first scene pass.
  SubmitImmediate([&](VkCommandBuffer cmd) {
    vk::TransitionImage(cmd, scene_color_.Get(), VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, 0,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  });
  scene_texture_set_ = ImGui_ImplVulkan_AddTexture(
      scene_sampler_, scene_color_.View(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  if (game_scene_) {
    game_scene_->SetWidth(static_cast<float>(scene_requested_.width));
    game_scene_->SetHeight(static_cast<float>(scene_requested_.height));
  }
}

void VulkanApplication::ReadCaptureSettings() {
  if (const char *path = std::getenv("MERCURY_CAPTURE")) capture_path_ = path;
  if (const char *frame = std::getenv("MERCURY_CAPTURE_FRAME")) {
    capture_frame_ = std::strtoull(frame, nullptr, 10);
  }
  if (const char *quit = std::getenv("MERCURY_CAPTURE_QUIT")) {
    capture_quit_ = std::string(quit) == "1";
  }
}

void VulkanApplication::WriteCapture() {
  vkQueueWaitIdle(context_.queue);
  const uint32_t w = capture_extent_.width;
  const uint32_t h = capture_extent_.height;
  std::vector<uint8_t> pixels(size_t{w} * h * 4);
  std::memcpy(pixels.data(), capture_buffer_.Mapped(), pixels.size());
  const VkFormat format = swapchain_.ColorFormat();
  if (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM) {
    for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
  }
  for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
  if (stbi_write_png(capture_path_.c_str(), static_cast<int>(w),
                     static_cast<int>(h), 4, pixels.data(),
                     static_cast<int>(w) * 4)) {
    PDebug::info("Saved capture to {}", capture_path_);
  } else {
    PDebug::error("Could not write capture to {}", capture_path_);
  }
  capture_buffer_.Reset();
  capture_path_.clear();
  if (capture_quit_) Quit();
}

void VulkanApplication::RecreateSwapchain() {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(GetWindow(), &width, &height);
  if (width == 0 || height == 0) return;  // minimized; keep the dirty flag

  vkDeviceWaitIdle(context_.device);
  const VkFormat old_format = swapchain_.ColorFormat();
  if (!swapchain_.Create(context_, static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height))) {
    PDebug::error("Vulkan: failed to recreate swapchain");
    return;
  }
  if (swapchain_.ColorFormat() != old_format) {
    PDebug::warn("Vulkan: swapchain format changed; pipelines may mismatch");
  }
  ImGui_ImplVulkan_SetMinImageCount(swapchain_.MinImageCount());
  swapchain_dirty_ = false;
}

void VulkanApplication::OnResize(int32_t width, int32_t height) {
  Application::OnResize(width, height);
  swapchain_dirty_ = true;
  // The scene keeps the size of its offscreen target, not of the window.
  const VkExtent2D target = scene_color_.Extent();
  if (scene_texture_enabled_ && game_scene_ && target.width > 0) {
    game_scene_->SetWidth(static_cast<float>(target.width));
    game_scene_->SetHeight(static_cast<float>(target.height));
  }
}

}  // namespace Mercury
