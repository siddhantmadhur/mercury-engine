#ifndef MERCURYENGINE_VULKAN_APPLICATION_H
#define MERCURYENGINE_VULKAN_APPLICATION_H

#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "application.h"
#include "globals.h"
#include "vulkan/vk_buffer.h"
#include "vulkan/vk_context.h"
#include "camera_buffer.h"
#include "vulkan/vk_scene_renderer.h"
#include "vulkan/vk_swapchain.h"
#include "vulkan/vk_ui_renderer.h"

namespace Mercury {

// Cached per-mesh draw-call info, rebuilt only when entities are added or
// removed. instance_count counts how many instance slots in the persistent
// instance_buffer_ belong to this mesh's draw group, starting at
// start_instance.
struct MeshLayoutEntry {
  UINT index_count = 0;
  UINT start_index = 0;
  UINT start_instance = 0;
  UINT instance_count = 0;
};

class VulkanApplication : public Application {
 public:
  VulkanApplication(const std::string& window_title,
                    float initial_width = 1280, float initial_height = 720);
  ~VulkanApplication() override;

  void EnableSceneTexture(bool enabled) override;
  void SetSceneViewportSize(int width, int height) override;
  uint64_t GetSceneTexture() const override;
  void SetPreviewCamera(kEntityId camera, int width, int height) override;
  uint64_t GetPreviewTexture() const override;
  void SetUIPreview(const UI::UIDrawData* draw_data, int width, int height,
                    glm::vec4 background) override;
  uint64_t GetUIPreviewTexture() const override;
  void SetShowRenderStats(bool show) override { show_render_stats_ = show; }

 protected:
  bool OnLoad() override;      // Runs when the application is created
  void Render() override;      // Renders objects
  bool Initialize() override;  // Creates the device, swapchain and ImGui
  void OnNewTick() override;

  void OnResize(int32_t width, int32_t height) override;
  void ImGuiNewFrame() override;

 private:
  static constexpr uint32_t kFramesInFlight = 2;
  static constexpr UINT kMaxInstances = 8192;
  static constexpr size_t kMaxVertices = 1 << 19;
  static constexpr size_t kMaxStaticVertices = 640000;
  static constexpr size_t kMaxIndices = 1 << 20;
  static constexpr VkDeviceSize kInstanceBufferBytes =
      sizeof(VsModelBuffer) * kMaxInstances;

  struct FrameData {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet shadow_descriptor_set = VK_NULL_HANDLE;
    // Same layout as descriptor_set, but bound to preview_camera_ubo.
    VkDescriptorSet preview_descriptor_set = VK_NULL_HANDLE;

    vk::Buffer camera_ubo;
    vk::Buffer preview_camera_ubo;
    vk::Buffer shadow_ubo;
    vk::Buffer vertices;
    vk::Buffer indices;
    vk::Buffer static_vertices;
    vk::Buffer static_indices;
    // Mirrors the instance buffer layout (slot N at N * sizeof(VsModelBuffer))
    // so each copy region uses the same src and dst offset.
    vk::Buffer instance_staging;
    // Only alive while an atlas upload recorded in this frame is in flight.
    vk::Buffer atlas_staging;
  };

  bool CreateFrameResources();
  bool CreateDescriptors();
  bool CreatePipelines();
  bool CreateShadowResources();
  bool CreateSkyboxResources();
  // Loads the scene's skybox panorama when its path changed. Waits for the
  // device to go idle, so it only runs between frames.
  void UpdateSkyboxImage();
  bool CreateAtlasImage(uint32_t width, uint32_t height);
  void WriteDescriptorSets();
  bool InitImGui();
  void RecreateSwapchain();
  VkShaderModule LoadShaderModule(const std::string& file_name) const;

  void RecordShadowDraws(VkCommandBuffer cmd, FrameData& frame);

  // Records a one-off command buffer and waits for it to finish.
  template <typename F>
  bool SubmitImmediate(F&& record);

  // Copies data into a mapped per-frame buffer, logging (once) and returning
  // false if it does not fit.
  bool UploadToFrameBuffer(vk::Buffer& buffer, const void* data,
                           VkDeviceSize bytes, const char* name);

  // Binds the pipelines and records every scene draw into the current
  // rendering pass, with a viewport covering `extent`. `camera_set` is a set
  // with the main layout; the skybox is drawn when `sky_matrix` is given.
  VkClearColorValue SceneClearColor() const;
  void RecordSceneDraws(VkCommandBuffer cmd, FrameData& frame,
                        VkDescriptorSet camera_set,
                        const glm::mat4x4* sky_matrix, VkExtent2D extent,
                        bool draw_static, UINT static_index_count,
                        bool draw_instanced);

  // Records a whole pass into an offscreen color + depth target and leaves
  // the color image ready for ImGui to sample.
  void RecordOffscreenPass(VkCommandBuffer cmd, FrameData& frame,
                           vk::Image& color, vk::Image& depth,
                           VkClearColorValue clear, VkDescriptorSet camera_set,
                           const glm::mat4x4* sky_matrix, bool draw_static,
                           UINT static_index_count, bool draw_instanced);

  // (Re)creates an offscreen color + depth target and its ImGui texture.
  // Waits for the device to go idle, so it only runs between frames.
  bool CreateOffscreenTarget(VkExtent2D extent, vk::Image& color,
                             vk::Image& depth, VkDescriptorSet& imgui_set);
  void DestroyOffscreenTarget(vk::Image& color, vk::Image& depth,
                              VkDescriptorSet& imgui_set);

  // (Re)creates the offscreen scene target at scene_requested_.
  void RecreateSceneTarget();
  void DestroySceneTarget();
  // (Re)creates the camera preview target at preview_requested_.
  void RecreatePreviewTarget();
  // Clears the UI preview target to its background and draws `batch`.
  void RecordUIPreviewPass(VkCommandBuffer cmd, const vk::UIRenderer::Batch& batch);

  // MERCURY_CAPTURE=<file.png> saves frame MERCURY_CAPTURE_FRAME (default
  // 90) of the window; MERCURY_CAPTURE_QUIT=1 closes the window afterwards.
  void ReadCaptureSettings();
  void WriteCapture();

  void RecordAtlasUpload(FrameData& frame, TextureAtlas& atlas);
  void RecordInstanceUploads(FrameData& frame);

  // Rebuild mesh_layout_/mesh_order_/entity_slot_ and stage the full
  // instance buffer for upload. Called on first frame and whenever an EnTT
  // lifecycle signal flips layout_dirty_.
  void RebuildInstanceLayout(entt::registry& registry, TextureAtlas& atlas,
                             FrameData& frame);

  // Walk every Transform marked dirty, build its VsModelBuffer, and stage a
  // copy of just that slot. No-op if nothing is dirty.
  void UpdateDirtyInstances(entt::registry& registry, TextureAtlas& atlas,
                            FrameData& frame);

  // Build a VsModelBuffer record for an instance. parent_mesh_entity is the
  // owning Mesh entity (== entity when the entity itself is a Mesh, or the
  // MeshInstance's parent_entity otherwise). Opacity comes from the parent
  // Mesh; atlas UV from entity's Texture2D, else parent's, else white pixel.
  VsModelBuffer BuildInstanceRecord(entt::registry& registry,
                                    TextureAtlas& atlas, entt::entity entity,
                                    entt::entity parent_mesh_entity);

  void EnsureRegistrySignalsHooked(entt::registry& registry);
  void OnLayoutChanged() { layout_dirty_ = true; }

  vk::Context context_;
  vk::Swapchain swapchain_;
  bool swapchain_dirty_ = false;

  std::array<FrameData, kFramesInFlight> frames_;
  uint32_t frame_index_ = 0;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline instanced_pipeline_ = VK_NULL_HANDLE;
  VkPipeline static_pipeline_ = VK_NULL_HANDLE;
  VkSampler texture_sampler_ = VK_NULL_HANDLE;

  static constexpr uint32_t kShadowMapResolution = 2048;
  vk::Image shadow_image_;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout shadow_pipeline_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout shadow_descriptor_set_layout_ = VK_NULL_HANDLE;

  // Equirectangular panorama drawn behind the scene; no image = clear color.
  vk::Image skybox_image_;
  std::string skybox_loaded_path_;
  VkSampler skybox_sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout skybox_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorSet skybox_set_ = VK_NULL_HANDLE;
  VkPipelineLayout skybox_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline skybox_pipeline_ = VK_NULL_HANDLE;

  vk::Buffer instance_buffer_;  // device-local, kMaxInstances records
  std::vector<VkBufferCopy> instance_copies_;  // staged this frame

  vk::Image atlas_image_;
  VkImageLayout atlas_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

  bool imgui_glfw_initialized_ = false;
  bool imgui_vulkan_initialized_ = false;
  bool overflow_logged_ = false;

  CameraCBuffer camera_ = {};
  std::vector<Vertex> vertex_buffer_;
  std::vector<UINT> index_buffer_;

  // Persistent instance layout. Tracked across frames so that frames where
  // nothing structurally changed can patch only the slots whose Transform
  // (or companion Texture2D) is dirty, instead of rebuilding every frame.
  std::unordered_map<entt::entity, MeshLayoutEntry> mesh_layout_;
  std::vector<entt::entity> mesh_order_;
  std::unordered_map<entt::entity, UINT> entity_slot_;
  std::vector<entt::entity> slot_owner_;   // slot index -> entity
  std::vector<entt::entity> slot_parent_;  // slot index -> parent mesh entity
  bool layout_dirty_ = true;
  entt::registry* hooked_registry_ = nullptr;

  // Offscreen scene target, shown by the editor inside an ImGui window.
  bool scene_texture_enabled_ = false;
  bool scene_target_dirty_ = false;
  VkExtent2D scene_requested_ = {0, 0};
  vk::Image scene_color_;
  vk::Image scene_depth_;
  VkSampler scene_sampler_ = VK_NULL_HANDLE;  // also samples the preview
  VkDescriptorSet scene_texture_set_ = VK_NULL_HANDLE;

  // Editor camera preview: the scene seen from preview_camera_, which the
  // editor sets every frame it shows the preview (reset after each frame).
  kEntityId preview_camera_ = entt::null;
  bool preview_target_dirty_ = false;
  VkExtent2D preview_requested_ = {0, 0};
  vk::Image preview_color_;
  vk::Image preview_depth_;
  VkDescriptorSet preview_texture_set_ = VK_NULL_HANDLE;

  // UI Toolkit: the game's UI over the scene, and the editor's UI Builder
  // canvas drawn into its own target (reset after each frame).
  vk::UIRenderer ui_renderer_;
  vk::SceneRenderer scene_renderer_;

  // The cameras to draw this frame at `extent`: the scene's, or (in the
  // editor, or without camera components) the current camera alone.
  std::vector<RenderCamera> FrameCameras(VkExtent2D extent);
  // Draws `cameras` with the model renderer (and the older draws) into an
  // image of the swapchain's format, leaving it as a color attachment.
  void RenderSceneTo(VkCommandBuffer cmd, FrameData& frame,
                     const std::vector<RenderCamera>& cameras, VkImage image,
                     VkImageView view, VkExtent2D extent, VkImageLayout layout,
                     VkDescriptorSet legacy_set, bool draw_static, UINT static_index_count,
                     bool draw_instanced);
  const UI::UIDrawData* ui_preview_data_ = nullptr;
  glm::vec4 ui_preview_background_ = glm::vec4(0.0f);
  bool ui_preview_target_dirty_ = false;
  VkExtent2D ui_preview_requested_ = {0, 0};
  vk::Image ui_preview_color_;
  vk::Image ui_preview_depth_;
  VkDescriptorSet ui_preview_texture_set_ = VK_NULL_HANDLE;

  bool show_render_stats_ = true;

  void RequestCapture(const std::string& file) override;
  std::string capture_path_;
  uint64_t capture_frame_ = 90;
  bool capture_quit_ = false;
  uint64_t frame_counter_ = 0;
  vk::Buffer capture_buffer_;
  VkExtent2D capture_extent_ = {};
};
}  // namespace Mercury

#endif  // MERCURYENGINE_VULKAN_APPLICATION_H
