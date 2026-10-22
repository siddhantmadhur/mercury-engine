#include "webgpu_application.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLFW/glfw3.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_wgpu.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "components/components.h"
#include "debugger/debugger.h"
#include "properties/mesh.h"
#include "properties/mesh_instance.h"
#include "properties/texture2d.h"
#include "properties/transform.h"
#include "stb/stb_image.h"

namespace Mercury {

namespace {

constexpr const char* kMainShaderWGSL = R"(
struct CameraUniforms {
    world_view_proj: mat4x4<f32>,
    light_view_proj: mat4x4<f32>,
    light_dir: vec4<f32>,
    light_color: vec4<f32>,
    ambient_color: vec4<f32>,
    camera_pos: vec4<f32>,
    resolution: vec2<f32>,
    shadow_params: vec2<f32>,
};

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(0) @binding(1) var diffuse_texture: texture_2d<f32>;
@group(0) @binding(2) var diffuse_sampler: sampler;
@group(0) @binding(3) var shadow_map: texture_depth_2d;
@group(0) @binding(4) var shadow_sampler: sampler_comparison;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(10) normal: vec3<f32>,

    @location(3) inst_scale: vec3<f32>,
    @location(4) inst_opacity: f32,
    @location(5) inst_world0: vec4<f32>,
    @location(6) inst_world1: vec4<f32>,
    @location(7) inst_world2: vec4<f32>,
    @location(8) inst_world3: vec4<f32>,
    @location(9) inst_atlas_uv: vec4<f32>,
    @location(11) inst_material: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) world_position: vec3<f32>,
    @location(4) material_params: vec2<f32>,
    // The texture's rectangle in the atlas (x0, y0, x1, y1); uv repeats
    // inside it, so tiling never bleeds into neighbouring images.
    @location(5) @interpolate(flat) atlas_rect: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let world = mat4x4<f32>(
        input.inst_world0,
        input.inst_world1,
        input.inst_world2,
        input.inst_world3
    );

    let scaled_pos = input.position * input.inst_scale;
    let world_pos = world * vec4<f32>(scaled_pos, 1.0);

    out.position = camera.world_view_proj * world_pos;
    out.world_position = world_pos.xyz;
    out.color = vec4<f32>(input.color, input.inst_opacity);
    out.uv = input.uv * input.inst_material.zw;
    out.atlas_rect = input.inst_atlas_uv;
    out.material_params = input.inst_material.xy;

    let norm_len = length(input.normal);
    if (norm_len > 0.001) {
        let n = (world * vec4<f32>(input.normal, 0.0)).xyz;
        out.normal = normalize(n);
    } else {
        out.normal = vec3<f32>(0.0, 0.0, 0.0);
    }

    return out;
}

const PI: f32 = 3.14159265358979323846;

fn DistributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;
    let denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

fn GeometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
}

fn GeometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    let ggx2 = GeometrySchlickGGX(NdotV, roughness);
    let ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

fn FresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (vec3<f32>(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn FresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    let max_f = max(vec3<f32>(1.0 - roughness), F0);
    return F0 + (max_f - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn CalculateShadow(world_pos: vec3<f32>, normal: vec3<f32>, to_light: vec3<f32>) -> f32 {
    if (camera.shadow_params.x <= 0.0) {
        return 1.0;
    }

    let NdotL = max(dot(normal, to_light), 0.0);
    if (NdotL <= 0.001) {
        return 0.0;
    }

    let normal_bias = max(camera.shadow_params.y, 0.02) * (1.0 - NdotL * 0.7);
    let biased_pos = world_pos + normal * normal_bias;
    let light_clip = camera.light_view_proj * vec4<f32>(biased_pos, 1.0);
    let proj = light_clip.xyz / light_clip.w;

    let shadow_uv = vec2<f32>(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    if (shadow_uv.x < 0.001 || shadow_uv.x > 0.999 ||
        shadow_uv.y < 0.001 || shadow_uv.y > 0.999 ||
        proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }

    let current_depth = proj.z;
    let texel = 1.0 / max(camera.shadow_params.x, 1024.0);

    var poisson_disk = array<vec2<f32>, 16>(
        vec2<f32>(-0.94201624, -0.39906216),
        vec2<f32>( 0.94558609, -0.76890725),
        vec2<f32>(-0.09418410, -0.92938870),
        vec2<f32>( 0.34495938,  0.29387760),
        vec2<f32>(-0.91588581,  0.45771432),
        vec2<f32>(-0.81544232, -0.87912464),
        vec2<f32>(-0.38277543,  0.27676845),
        vec2<f32>( 0.97484398,  0.75648379),
        vec2<f32>( 0.44323325, -0.97511554),
        vec2<f32>( 0.53742981, -0.47373420),
        vec2<f32>(-0.26496911, -0.41893023),
        vec2<f32>( 0.79197514,  0.19090188),
        vec2<f32>(-0.24188840,  0.99706507),
        vec2<f32>(-0.81409955,  0.91437590),
        vec2<f32>( 0.19984126,  0.78641367),
        vec2<f32>( 0.14383161, -0.14100790)
    );

    var shadow: f32 = 0.0;
    for (var i: i32 = 0; i < 16; i = i + 1) {
        let offset = poisson_disk[i] * (texel * 1.5);
        shadow = shadow + textureSampleCompareLevel(
            shadow_map,
            shadow_sampler,
            shadow_uv + offset,
            current_depth
        );
    }
    return shadow / 16.0;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // uv counts tiles; wrap it into the texture's rectangle in the atlas.
    let atlas_uv = input.atlas_rect.xy
                 + fract(input.uv) * (input.atlas_rect.zw - input.atlas_rect.xy);
    let base = textureSample(diffuse_texture, diffuse_sampler, atlas_uv) * input.color;

    let normal_len = length(input.normal);
    if (normal_len < 0.001) {
        return base;
    }

    let N = input.normal / normal_len;
    let V = normalize(camera.camera_pos.xyz - input.world_position);
    let to_light = -normalize(camera.light_dir.xyz);
    let H = normalize(V + to_light);

    let roughness = clamp(input.material_params.x, 0.04, 1.0);
    let metallic = clamp(input.material_params.y, 0.0, 1.0);

    let albedo = base.rgb;
    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    let NdotL = max(dot(N, to_light), 0.0);
    let NdotV = max(dot(N, V), 1e-4);

    let D = DistributionGGX(N, H, roughness);
    let G = GeometrySmith(N, V, to_light, roughness);
    let F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    let kS = F;
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

    let diffuse = kD * (albedo / PI);
    let specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    let shadow = CalculateShadow(input.world_position, N, to_light);
    let direct = (diffuse + specular) * camera.light_color.rgb * NdotL * shadow;

    let hemi = dot(N, vec3<f32>(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    let sky_ambient = camera.ambient_color.rgb;
    let ground_ambient = camera.ambient_color.rgb * 0.35;
    let ambient_light = mix(ground_ambient, sky_ambient, hemi);

    let F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
    let kD_ambient = (vec3<f32>(1.0) - F_ambient) * (1.0 - metallic);
    let ambient_diffuse = kD_ambient * albedo * ambient_light;
    let ambient_specular = F_ambient * ambient_light * (1.0 - roughness * 0.5);
    let ambient = ambient_diffuse + ambient_specular;

    let lit = direct + ambient;

    // ACES Filmic Tone Mapping
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    let mapped = clamp((lit * (a * lit + b)) / (lit * (c * lit + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));

    // sRGB Gamma Correction
    let gamma_corrected = pow(mapped, vec3<f32>(1.0 / 2.2));

    return vec4<f32>(gamma_corrected, base.a);
}
)";

constexpr const char* kShadowShaderWGSL = R"(
struct ShadowUniforms {
    light_view_proj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> shadow_uniforms: ShadowUniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(3) inst_scale: vec3<f32>,
    @location(5) inst_world0: vec4<f32>,
    @location(6) inst_world1: vec4<f32>,
    @location(7) inst_world2: vec4<f32>,
    @location(8) inst_world3: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4<f32> {
    let world = mat4x4<f32>(
        input.inst_world0,
        input.inst_world1,
        input.inst_world2,
        input.inst_world3
    );
    let scaled_pos = input.position * input.inst_scale;
    let world_pos = world * vec4<f32>(scaled_pos, 1.0);
    return shadow_uniforms.light_view_proj * world_pos;
}
)";

constexpr const char* kStaticShaderWGSL = R"(
struct CameraUniforms {
    world_view_proj: mat4x4<f32>,
    light_view_proj: mat4x4<f32>,
    light_dir: vec4<f32>,
    light_color: vec4<f32>,
    ambient_color: vec4<f32>,
    camera_pos: vec4<f32>,
    resolution: vec2<f32>,
    shadow_params: vec2<f32>,
};

@group(0) @binding(0) var<uniform> camera: CameraUniforms;

struct StaticVertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) atlas_uv: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) world_position: vec3<f32>,
    @location(4) material_params: vec2<f32>,
    // The texture's rectangle in the atlas (x0, y0, x1, y1); uv repeats
    // inside it, so tiling never bleeds into neighbouring images.
    @location(5) @interpolate(flat) atlas_rect: vec4<f32>,
};

@vertex
fn vs_main(input: StaticVertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = camera.world_view_proj * vec4<f32>(input.position, 1.0);
    out.color = input.color;
    out.uv = input.uv;
    out.atlas_rect = input.atlas_uv;
    out.normal = vec3<f32>(0.0, 0.0, 0.0);
    out.world_position = input.position;
    out.material_params = vec2<f32>(0.5, 0.0);
    return out;
}
)";

// Mirrors shaders/webgpu/skybox.wgsl.
constexpr const char* kSkyboxShaderWGSL = R"(
struct SkyboxUniforms {
    inv_view_proj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> skybox: SkyboxUniforms;
@group(0) @binding(1) var panorama: texture_2d<f32>;
@group(0) @binding(2) var panorama_sampler: sampler;

const PI: f32 = 3.14159265;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

// One triangle covering the screen, built from the vertex index.
@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    let ndc = vec2<f32>(f32((vertex_index << 1u) & 2u),
                        f32(vertex_index & 2u)) * 2.0 - 1.0;
    var out: VertexOutput;
    out.position = vec4<f32>(ndc, 1.0, 1.0);
    out.ndc = ndc;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let far_point = skybox.inv_view_proj * vec4<f32>(input.ndc, 1.0, 1.0);
    let dir = normalize(far_point.xyz / far_point.w);

    // Longitude around +Y (the image centre faces -Z), latitude from the top.
    let uv = vec2<f32>(atan2(dir.x, -dir.z) / (2.0 * PI) + 0.5,
                       acos(clamp(dir.y, -1.0, 1.0)) / PI);
    // Explicit LOD: the derivative of uv.x jumps across the seam.
    return vec4<f32>(textureSampleLevel(panorama, panorama_sampler, uv, 0.0).rgb, 1.0);
}
)";

}  // namespace

WebGpuApplication::WebGpuApplication(const std::string& window_title,
                                     float initial_width, float initial_height)
    : Application(window_title, initial_width, initial_height) {}

WebGpuApplication::~WebGpuApplication() {
  if (device_ready_) {
    ImGui_ImplWGPU_Shutdown();
  }
  ImGui_ImplGlfw_Shutdown();
}

bool WebGpuApplication::Initialize() {
  if (!Application::Initialize()) return false;
  ImGui_ImplGlfw_InitForOther(GetWindow(), true);
  return CreateDeviceAndSurface();
}

bool WebGpuApplication::CreateDeviceAndSurface() {
  instance_ = wgpu::CreateInstance();
  if (!instance_) {
    PDebug::error("WebGPU: failed to create instance");
    return false;
  }

#if defined(__EMSCRIPTEN__)
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_source;
  canvas_source.selector = "#canvas";
  wgpu::SurfaceDescriptor surface_desc;
  surface_desc.nextInChain = &canvas_source;
  surface_ = instance_.CreateSurface(&surface_desc);
#else
  surface_ = nullptr;
#endif

  if (!surface_) {
    PDebug::error("WebGPU: failed to create canvas surface");
    return false;
  }

  wgpu::RequestAdapterOptions adapter_opts = {};
  adapter_opts.compatibleSurface = surface_;

  instance_.RequestAdapter(
      &adapter_opts, wgpu::CallbackMode::AllowSpontaneous,
      [this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
             wgpu::StringView message) {
        if (status != wgpu::RequestAdapterStatus::Success || !adapter) {
          PDebug::error("WebGPU: RequestAdapter failed: {}",
                        message.data ? message.data : "unknown");
          return;
        }
        adapter_ = adapter;

        wgpu::DeviceDescriptor device_desc = {};
        device_desc.SetUncapturedErrorCallback(
            [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView msg) {
              PDebug::error("WebGPU Device Error (type {}): {}", static_cast<int>(type),
                            msg.data ? msg.data : "unknown");
            });

        adapter_.RequestDevice(
            &device_desc, wgpu::CallbackMode::AllowSpontaneous,
            [this](wgpu::RequestDeviceStatus dev_status, wgpu::Device device,
                   wgpu::StringView dev_message) {
              if (dev_status != wgpu::RequestDeviceStatus::Success || !device) {
                PDebug::error("WebGPU: RequestDevice failed: {}",
                              dev_message.data ? dev_message.data : "unknown");
                return;
              }
              device_ = device;
              OnDeviceReady();
            });
      });

  return true;
}

void WebGpuApplication::OnDeviceReady() {
  queue_ = device_.GetQueue();

  wgpu::SurfaceCapabilities caps;
  surface_.GetCapabilities(adapter_, &caps);
  if (caps.formatCount > 0) {
    surface_format_ = caps.formats[0];
  } else {
    surface_format_ = wgpu::TextureFormat::BGRA8Unorm;
  }

#if defined(__EMSCRIPTEN__)
  int width = EM_ASM_INT({
    var c = document.getElementById('canvas');
    return c ? c.width : 0;
  });
  int height = EM_ASM_INT({
    var c = document.getElementById('canvas');
    return c ? c.height : 0;
  });
#else
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(GetWindow(), &width, &height);
#endif
  if (width <= 0 || height <= 0) {
    width = GetWidth();
    height = GetHeight();
  }
  if (width <= 0 || height <= 0) {
    width = 1280;
    height = 720;
  }
  ConfigureSurface(width, height);

  if (!CreateBuffers()) {
    PDebug::error("WebGPU: failed to create buffers");
    return;
  }
  if (!CreateShadowResources()) {
    PDebug::error("WebGPU: failed to create shadow resources");
    return;
  }

  if (game_scene_) {
    TextureAtlas& atlas = game_scene_->GetAtlas();
    CreateAtlasTexture(atlas.GetWidth(), atlas.GetHeight());
  } else {
    CreateAtlasTexture(2048, 2048);
  }

  if (!CreatePipelines()) {
    PDebug::error("WebGPU: failed to create pipelines");
    return;
  }

  // ImGui WGPU backend setup
  ImGui_ImplWGPU_InitInfo init_info = {};
  init_info.Device = device_.Get();
  init_info.NumFramesInFlight = 1;
  init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(surface_format_);
  init_info.DepthStencilFormat = WGPUTextureFormat_Depth32Float;
  if (!ImGui_ImplWGPU_Init(&init_info)) {
    PDebug::error("WebGPU: failed to initialize ImGui WGPU backend");
    return;
  }

  if (game_scene_) {
    EnsureRegistrySignalsHooked(game_scene_->GetRegistry());
    layout_dirty_ = true;
    RebuildInstanceLayout(game_scene_->GetRegistry(), game_scene_->GetAtlas());
  }

  device_ready_ = true;
  PDebug::info("WebGPU: device ready and initialization completed");
}

bool WebGpuApplication::ConfigureSurface(int width, int height) {
  if (width <= 0 || height <= 0 || !device_) return false;

  wgpu::SurfaceConfiguration config = {};
  config.device = device_;
  config.format = surface_format_;
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.width = static_cast<uint32_t>(width);
  config.height = static_cast<uint32_t>(height);
  config.presentMode = wgpu::PresentMode::Fifo;
  surface_.Configure(&config);

  wgpu::TextureDescriptor depth_desc = {};
  depth_desc.format = wgpu::TextureFormat::Depth32Float;
  depth_desc.usage = wgpu::TextureUsage::RenderAttachment;
  depth_desc.size = {static_cast<uint32_t>(width),
                     static_cast<uint32_t>(height), 1};
  depth_texture_ = device_.CreateTexture(&depth_desc);
  depth_view_ = depth_texture_.CreateView();

  return true;
}

bool WebGpuApplication::OnLoad() {
  return true;
}

bool WebGpuApplication::CreateShadowResources() {
  wgpu::TextureDescriptor shadow_desc = {};
  shadow_desc.format = wgpu::TextureFormat::Depth32Float;
  shadow_desc.usage = wgpu::TextureUsage::RenderAttachment |
                      wgpu::TextureUsage::TextureBinding;
  shadow_desc.size = {kShadowMapResolution, kShadowMapResolution, 1};
  shadow_texture_ = device_.CreateTexture(&shadow_desc);
  shadow_view_ = shadow_texture_.CreateView();

  wgpu::SamplerDescriptor sampler_desc = {};
  sampler_desc.compare = wgpu::CompareFunction::LessEqual;
  sampler_desc.magFilter = wgpu::FilterMode::Linear;
  sampler_desc.minFilter = wgpu::FilterMode::Linear;
  shadow_sampler_ = device_.CreateSampler(&sampler_desc);

  return true;
}

bool WebGpuApplication::CreateAtlasTexture(uint32_t width, uint32_t height) {
  atlas_width_ = width;
  atlas_height_ = height;

  wgpu::TextureDescriptor desc = {};
  desc.format = wgpu::TextureFormat::RGBA8Unorm;
  desc.usage =
      wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  desc.size = {width, height, 1};
  atlas_texture_ = device_.CreateTexture(&desc);
  atlas_view_ = atlas_texture_.CreateView();

  wgpu::SamplerDescriptor sampler_desc = {};
  sampler_desc.magFilter = wgpu::FilterMode::Nearest;
  sampler_desc.minFilter = wgpu::FilterMode::Nearest;
  atlas_sampler_ = device_.CreateSampler(&sampler_desc);

  UpdateAtlasTexture();
  return true;
}

void WebGpuApplication::UpdateAtlasTexture() {
  if (!game_scene_) return;
  TextureAtlas& atlas = game_scene_->GetAtlas();
  const auto* data = atlas.GetData();
  if (!data) return;

  uint32_t width = static_cast<uint32_t>(atlas.GetWidth());
  uint32_t height = static_cast<uint32_t>(atlas.GetHeight());
  if (width == 0 || height == 0) return;

  if (width != atlas_width_ || height != atlas_height_) {
    atlas_width_ = width;
    atlas_height_ = height;
    CreateAtlasTexture(width, height);
    return;
  }

  wgpu::TexelCopyTextureInfo dst = {};
  dst.texture = atlas_texture_;

  wgpu::TexelCopyBufferLayout layout = {};
  layout.bytesPerRow = atlas_width_ * 4;
  layout.rowsPerImage = atlas_height_;

  wgpu::Extent3D extent = {atlas_width_, atlas_height_, 1};
  queue_.WriteTexture(&dst, data, atlas_width_ * atlas_height_ * 4, &layout, &extent);
  atlas.ClearGpuUploadFlag();
}

void WebGpuApplication::UpdateSkyboxTexture() {
  const std::string& path = game_scene_->GetSkyboxPath();
  if (path == skybox_loaded_path_) return;
  // Set first, so a file that fails to load is only reported once.
  skybox_loaded_path_ = path;
  skybox_bind_group_ = nullptr;
  skybox_texture_ = nullptr;
  if (path.empty()) return;

  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
  if (!pixels) {
    PDebug::error("Could not load skybox image: {}", path);
    return;
  }

  wgpu::TextureDescriptor desc = {};
  desc.format = wgpu::TextureFormat::RGBA8Unorm;
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
  skybox_texture_ = device_.CreateTexture(&desc);

  wgpu::TexelCopyTextureInfo dst = {};
  dst.texture = skybox_texture_;
  wgpu::TexelCopyBufferLayout layout = {};
  layout.bytesPerRow = desc.size.width * 4;
  layout.rowsPerImage = desc.size.height;
  queue_.WriteTexture(&dst, pixels, size_t(width) * height * 4, &layout,
                      &desc.size);
  stbi_image_free(pixels);

  wgpu::BindGroupEntry entries[3];
  entries[0].binding = 0;
  entries[0].buffer = skybox_ubo_;
  entries[0].size = sizeof(glm::mat4);
  entries[1].binding = 1;
  entries[1].textureView = skybox_texture_.CreateView();
  entries[2].binding = 2;
  entries[2].sampler = skybox_sampler_;
  wgpu::BindGroupDescriptor bg_desc = {};
  bg_desc.layout = skybox_bind_group_layout_;
  bg_desc.entryCount = 3;
  bg_desc.entries = entries;
  skybox_bind_group_ = device_.CreateBindGroup(&bg_desc);
  PDebug::log("Loaded skybox: {} [{}x{}]", path, width, height);
}

bool WebGpuApplication::CreateBuffers() {
  // Uniform buffers
  wgpu::BufferDescriptor ubo_desc = {};
  ubo_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  ubo_desc.size = sizeof(CameraCBuffer);
  camera_ubo_ = device_.CreateBuffer(&ubo_desc);

  wgpu::BufferDescriptor shadow_ubo_desc = {};
  shadow_ubo_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  shadow_ubo_desc.size = sizeof(glm::mat4);
  shadow_ubo_ = device_.CreateBuffer(&shadow_ubo_desc);

  wgpu::BufferDescriptor skybox_ubo_desc = {};
  skybox_ubo_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  skybox_ubo_desc.size = sizeof(glm::mat4);
  skybox_ubo_ = device_.CreateBuffer(&skybox_ubo_desc);

  // Geometry buffers
  wgpu::BufferDescriptor vbo_desc = {};
  vbo_desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  vbo_desc.size = sizeof(Vertex) * kMaxVertices;
  vertex_buffer_ = device_.CreateBuffer(&vbo_desc);

  wgpu::BufferDescriptor ibo_desc = {};
  ibo_desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  ibo_desc.size = sizeof(UINT) * kMaxIndices;
  index_buffer_ = device_.CreateBuffer(&ibo_desc);

  wgpu::BufferDescriptor inst_desc = {};
  inst_desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  inst_desc.size = sizeof(VsModelBuffer) * kMaxInstances;
  instance_buffer_ = device_.CreateBuffer(&inst_desc);

  wgpu::BufferDescriptor static_vbo_desc = {};
  static_vbo_desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  static_vbo_desc.size = sizeof(StaticVertex) * 32768;
  static_vertex_buffer_ = device_.CreateBuffer(&static_vbo_desc);

  wgpu::BufferDescriptor static_ibo_desc = {};
  static_ibo_desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  static_ibo_desc.size = sizeof(UINT) * 65536;
  static_index_buffer_ = device_.CreateBuffer(&static_ibo_desc);

  return true;
}

bool WebGpuApplication::CreatePipelines() {
  // 1. Shaders
  wgpu::ShaderSourceWGSL main_wgsl = {};
  main_wgsl.code = kMainShaderWGSL;
  wgpu::ShaderModuleDescriptor main_module_desc = {};
  main_module_desc.nextInChain = &main_wgsl;
  auto main_module = device_.CreateShaderModule(&main_module_desc);

  wgpu::ShaderSourceWGSL shadow_wgsl = {};
  shadow_wgsl.code = kShadowShaderWGSL;
  wgpu::ShaderModuleDescriptor shadow_module_desc = {};
  shadow_module_desc.nextInChain = &shadow_wgsl;
  auto shadow_module = device_.CreateShaderModule(&shadow_module_desc);

  wgpu::ShaderSourceWGSL static_wgsl = {};
  static_wgsl.code = kStaticShaderWGSL;
  wgpu::ShaderModuleDescriptor static_module_desc = {};
  static_module_desc.nextInChain = &static_wgsl;
  auto static_module = device_.CreateShaderModule(&static_module_desc);

  // 2. Bind group layout for Main Scene Pass
  wgpu::BindGroupLayoutEntry entries[5];
  // 0: Camera UBO
  entries[0].binding = 0;
  entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  entries[0].buffer.minBindingSize = sizeof(CameraCBuffer);
  // 1: Diffuse Texture
  entries[1].binding = 1;
  entries[1].visibility = wgpu::ShaderStage::Fragment;
  entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
  entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  // 2: Diffuse Sampler
  entries[2].binding = 2;
  entries[2].visibility = wgpu::ShaderStage::Fragment;
  entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
  // 3: Shadow Map
  entries[3].binding = 3;
  entries[3].visibility = wgpu::ShaderStage::Fragment;
  entries[3].texture.sampleType = wgpu::TextureSampleType::Depth;
  entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  // 4: Shadow Sampler
  entries[4].binding = 4;
  entries[4].visibility = wgpu::ShaderStage::Fragment;
  entries[4].sampler.type = wgpu::SamplerBindingType::Comparison;

  wgpu::BindGroupLayoutDescriptor main_bgl_desc = {};
  main_bgl_desc.entryCount = 5;
  main_bgl_desc.entries = entries;
  main_bind_group_layout_ = device_.CreateBindGroupLayout(&main_bgl_desc);

  // 3. Bind group layout for Shadow Pass
  wgpu::BindGroupLayoutEntry shadow_entries[1];
  shadow_entries[0].binding = 0;
  shadow_entries[0].visibility = wgpu::ShaderStage::Vertex;
  shadow_entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  shadow_entries[0].buffer.minBindingSize = sizeof(glm::mat4);

  wgpu::BindGroupLayoutDescriptor shadow_bgl_desc = {};
  shadow_bgl_desc.entryCount = 1;
  shadow_bgl_desc.entries = shadow_entries;
  shadow_bind_group_layout_ = device_.CreateBindGroupLayout(&shadow_bgl_desc);

  // Bind groups
  wgpu::BindGroupEntry bg_entries[5];
  bg_entries[0].binding = 0;
  bg_entries[0].buffer = camera_ubo_;
  bg_entries[0].size = sizeof(CameraCBuffer);
  bg_entries[1].binding = 1;
  bg_entries[1].textureView = atlas_view_;
  bg_entries[2].binding = 2;
  bg_entries[2].sampler = atlas_sampler_;
  bg_entries[3].binding = 3;
  bg_entries[3].textureView = shadow_view_;
  bg_entries[4].binding = 4;
  bg_entries[4].sampler = shadow_sampler_;

  wgpu::BindGroupDescriptor main_bg_desc = {};
  main_bg_desc.layout = main_bind_group_layout_;
  main_bg_desc.entryCount = 5;
  main_bg_desc.entries = bg_entries;
  main_bind_group_ = device_.CreateBindGroup(&main_bg_desc);

  wgpu::BindGroupEntry shadow_bg_entry;
  shadow_bg_entry.binding = 0;
  shadow_bg_entry.buffer = shadow_ubo_;
  shadow_bg_entry.size = sizeof(glm::mat4);

  wgpu::BindGroupDescriptor shadow_bg_desc = {};
  shadow_bg_desc.layout = shadow_bind_group_layout_;
  shadow_bg_desc.entryCount = 1;
  shadow_bg_desc.entries = &shadow_bg_entry;
  shadow_bind_group_ = device_.CreateBindGroup(&shadow_bg_desc);

  // Pipeline layout
  wgpu::PipelineLayoutDescriptor main_pl_desc = {};
  main_pl_desc.bindGroupLayoutCount = 1;
  main_pl_desc.bindGroupLayouts = &main_bind_group_layout_;
  auto main_pipeline_layout = device_.CreatePipelineLayout(&main_pl_desc);

  wgpu::PipelineLayoutDescriptor shadow_pl_desc = {};
  shadow_pl_desc.bindGroupLayoutCount = 1;
  shadow_pl_desc.bindGroupLayouts = &shadow_bind_group_layout_;
  auto shadow_pipeline_layout = device_.CreatePipelineLayout(&shadow_pl_desc);

  auto make_attr = [](wgpu::VertexFormat format, uint64_t offset, uint32_t location) {
    wgpu::VertexAttribute a;
    a.format = format;
    a.offset = offset;
    a.shaderLocation = location;
    return a;
  };

  // Vertex layouts for Instanced pipeline
  wgpu::VertexAttribute vertex_attribs[4];
  vertex_attribs[0] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(Vertex, position), 0);
  vertex_attribs[1] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(Vertex, color), 1);
  vertex_attribs[2] = make_attr(wgpu::VertexFormat::Float32x2, offsetof(Vertex, uv), 2);
  vertex_attribs[3] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(Vertex, normal), 10);

  wgpu::VertexAttribute instance_attribs[8];
  instance_attribs[0] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(VsModelBuffer, scale), 3);
  instance_attribs[1] = make_attr(wgpu::VertexFormat::Float32, offsetof(VsModelBuffer, opacity), 4);
  instance_attribs[2] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_), 5);
  instance_attribs[3] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 16, 6);
  instance_attribs[4] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 32, 7);
  instance_attribs[5] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 48, 8);
  instance_attribs[6] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, atlas_uv), 9);
  instance_attribs[7] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, material_params), 11);

  wgpu::VertexBufferLayout instanced_vbo_layouts[2] = {};
  instanced_vbo_layouts[0].arrayStride = sizeof(Vertex);
  instanced_vbo_layouts[0].stepMode = wgpu::VertexStepMode::Vertex;
  instanced_vbo_layouts[0].attributeCount = 4;
  instanced_vbo_layouts[0].attributes = vertex_attribs;

  instanced_vbo_layouts[1].arrayStride = sizeof(VsModelBuffer);
  instanced_vbo_layouts[1].stepMode = wgpu::VertexStepMode::Instance;
  instanced_vbo_layouts[1].attributeCount = 8;
  instanced_vbo_layouts[1].attributes = instance_attribs;

  // Instanced Graphics Pipeline
  wgpu::ColorTargetState color_target = {};
  color_target.format = surface_format_;
  wgpu::BlendState blend = {};
  blend.color.operation = wgpu::BlendOperation::Add;
  blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
  blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  blend.alpha.operation = wgpu::BlendOperation::Add;
  blend.alpha.srcFactor = wgpu::BlendFactor::One;
  blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
  color_target.blend = &blend;

  wgpu::FragmentState fragment_state = {};
  fragment_state.module = main_module;
  fragment_state.entryPoint = "fs_main";
  fragment_state.targetCount = 1;
  fragment_state.targets = &color_target;

  wgpu::DepthStencilState depth_state = {};
  depth_state.format = wgpu::TextureFormat::Depth32Float;
  depth_state.depthWriteEnabled = true;
  depth_state.depthCompare = wgpu::CompareFunction::LessEqual;

  wgpu::RenderPipelineDescriptor instanced_pipe_desc = {};
  instanced_pipe_desc.layout = main_pipeline_layout;
  instanced_pipe_desc.vertex.module = main_module;
  instanced_pipe_desc.vertex.entryPoint = "vs_main";
  instanced_pipe_desc.vertex.bufferCount = 2;
  instanced_pipe_desc.vertex.buffers = instanced_vbo_layouts;
  instanced_pipe_desc.fragment = &fragment_state;
  instanced_pipe_desc.depthStencil = &depth_state;
  instanced_pipe_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  instanced_pipe_desc.primitive.cullMode = wgpu::CullMode::Back;
  instanced_pipe_desc.primitive.frontFace = wgpu::FrontFace::CCW;
  instanced_pipeline_ = device_.CreateRenderPipeline(&instanced_pipe_desc);

  // Shadow Graphics Pipeline
  wgpu::VertexAttribute shadow_vertex_attribs[1];
  shadow_vertex_attribs[0] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(Vertex, position), 0);

  wgpu::VertexAttribute shadow_instance_attribs[5];
  shadow_instance_attribs[0] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(VsModelBuffer, scale), 3);
  shadow_instance_attribs[1] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_), 5);
  shadow_instance_attribs[2] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 16, 6);
  shadow_instance_attribs[3] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 32, 7);
  shadow_instance_attribs[4] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(VsModelBuffer, world_) + 48, 8);

  wgpu::VertexBufferLayout shadow_vbo_layouts[2] = {};
  shadow_vbo_layouts[0].arrayStride = sizeof(Vertex);
  shadow_vbo_layouts[0].stepMode = wgpu::VertexStepMode::Vertex;
  shadow_vbo_layouts[0].attributeCount = 1;
  shadow_vbo_layouts[0].attributes = shadow_vertex_attribs;

  shadow_vbo_layouts[1].arrayStride = sizeof(VsModelBuffer);
  shadow_vbo_layouts[1].stepMode = wgpu::VertexStepMode::Instance;
  shadow_vbo_layouts[1].attributeCount = 5;
  shadow_vbo_layouts[1].attributes = shadow_instance_attribs;

  wgpu::DepthStencilState shadow_depth_state = {};
  shadow_depth_state.format = wgpu::TextureFormat::Depth32Float;
  shadow_depth_state.depthWriteEnabled = true;
  shadow_depth_state.depthCompare = wgpu::CompareFunction::LessEqual;
  shadow_depth_state.depthBias = 1;
  shadow_depth_state.depthBiasSlopeScale = 1.75f;

  wgpu::RenderPipelineDescriptor shadow_pipe_desc = {};
  shadow_pipe_desc.layout = shadow_pipeline_layout;
  shadow_pipe_desc.vertex.module = shadow_module;
  shadow_pipe_desc.vertex.entryPoint = "vs_main";
  shadow_pipe_desc.vertex.bufferCount = 2;
  shadow_pipe_desc.vertex.buffers = shadow_vbo_layouts;
  shadow_pipe_desc.depthStencil = &shadow_depth_state;
  shadow_pipe_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  shadow_pipe_desc.primitive.cullMode = wgpu::CullMode::Back;
  shadow_pipe_desc.primitive.frontFace = wgpu::FrontFace::CCW;
  shadow_pipeline_ = device_.CreateRenderPipeline(&shadow_pipe_desc);

  // Static Graphics Pipeline
  wgpu::VertexAttribute static_attribs[4];
  static_attribs[0] = make_attr(wgpu::VertexFormat::Float32x3, offsetof(StaticVertex, position), 0);
  static_attribs[1] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(StaticVertex, color), 1);
  static_attribs[2] = make_attr(wgpu::VertexFormat::Float32x2, offsetof(StaticVertex, uv), 2);
  static_attribs[3] = make_attr(wgpu::VertexFormat::Float32x4, offsetof(StaticVertex, atlas_uv), 3);

  wgpu::VertexBufferLayout static_vbo_layout = {};
  static_vbo_layout.arrayStride = sizeof(StaticVertex);
  static_vbo_layout.stepMode = wgpu::VertexStepMode::Vertex;
  static_vbo_layout.attributeCount = 4;
  static_vbo_layout.attributes = static_attribs;

  wgpu::RenderPipelineDescriptor static_pipe_desc = {};
  static_pipe_desc.layout = main_pipeline_layout;
  static_pipe_desc.vertex.module = static_module;
  static_pipe_desc.vertex.entryPoint = "vs_main";
  static_pipe_desc.vertex.bufferCount = 1;
  static_pipe_desc.vertex.buffers = &static_vbo_layout;
  static_pipe_desc.fragment = &fragment_state;
  static_pipe_desc.depthStencil = &depth_state;
  static_pipe_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  static_pipe_desc.primitive.cullMode = wgpu::CullMode::Back;
  static_pipe_desc.primitive.frontFace = wgpu::FrontFace::CCW;
  static_pipeline_ = device_.CreateRenderPipeline(&static_pipe_desc);

  // Skybox pipeline: a fullscreen triangle from the vertex index, drawn
  // first without touching depth. The bind group is made per image in
  // UpdateSkyboxTexture().
  wgpu::ShaderSourceWGSL skybox_wgsl = {};
  skybox_wgsl.code = kSkyboxShaderWGSL;
  wgpu::ShaderModuleDescriptor skybox_module_desc = {};
  skybox_module_desc.nextInChain = &skybox_wgsl;
  auto skybox_module = device_.CreateShaderModule(&skybox_module_desc);

  wgpu::BindGroupLayoutEntry skybox_entries[3];
  skybox_entries[0].binding = 0;
  skybox_entries[0].visibility = wgpu::ShaderStage::Fragment;
  skybox_entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  skybox_entries[0].buffer.minBindingSize = sizeof(glm::mat4);
  skybox_entries[1].binding = 1;
  skybox_entries[1].visibility = wgpu::ShaderStage::Fragment;
  skybox_entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
  skybox_entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  skybox_entries[2].binding = 2;
  skybox_entries[2].visibility = wgpu::ShaderStage::Fragment;
  skybox_entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
  wgpu::BindGroupLayoutDescriptor skybox_bgl_desc = {};
  skybox_bgl_desc.entryCount = 3;
  skybox_bgl_desc.entries = skybox_entries;
  skybox_bind_group_layout_ = device_.CreateBindGroupLayout(&skybox_bgl_desc);

  wgpu::PipelineLayoutDescriptor skybox_pl_desc = {};
  skybox_pl_desc.bindGroupLayoutCount = 1;
  skybox_pl_desc.bindGroupLayouts = &skybox_bind_group_layout_;
  auto skybox_pipeline_layout = device_.CreatePipelineLayout(&skybox_pl_desc);

  // Wrap around horizontally so the panorama's seam is invisible.
  wgpu::SamplerDescriptor skybox_sampler_desc = {};
  skybox_sampler_desc.magFilter = wgpu::FilterMode::Linear;
  skybox_sampler_desc.minFilter = wgpu::FilterMode::Linear;
  skybox_sampler_desc.addressModeU = wgpu::AddressMode::Repeat;
  skybox_sampler_ = device_.CreateSampler(&skybox_sampler_desc);

  wgpu::ColorTargetState skybox_target = {};
  skybox_target.format = surface_format_;
  wgpu::FragmentState skybox_fragment = {};
  skybox_fragment.module = skybox_module;
  skybox_fragment.entryPoint = "fs_main";
  skybox_fragment.targetCount = 1;
  skybox_fragment.targets = &skybox_target;

  wgpu::DepthStencilState skybox_depth_state = {};
  skybox_depth_state.format = wgpu::TextureFormat::Depth32Float;
  skybox_depth_state.depthWriteEnabled = false;
  skybox_depth_state.depthCompare = wgpu::CompareFunction::Always;

  wgpu::RenderPipelineDescriptor skybox_pipe_desc = {};
  skybox_pipe_desc.layout = skybox_pipeline_layout;
  skybox_pipe_desc.vertex.module = skybox_module;
  skybox_pipe_desc.vertex.entryPoint = "vs_main";
  skybox_pipe_desc.fragment = &skybox_fragment;
  skybox_pipe_desc.depthStencil = &skybox_depth_state;
  skybox_pipe_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  skybox_pipe_desc.primitive.cullMode = wgpu::CullMode::None;
  skybox_pipeline_ = device_.CreateRenderPipeline(&skybox_pipe_desc);

  return true;
}

VsModelBuffer WebGpuApplication::BuildInstanceRecord(
    entt::registry& registry, TextureAtlas& atlas, entt::entity entity,
    entt::entity parent_mesh_entity) {
  VsModelBuffer record = {};
  auto* transform = registry.try_get<Transform>(entity);
  if (!transform) return record;

  record.scale = transform->GetInterpolatedScale();
  auto position = transform->GetInterpolatedPosition();
  glm::mat4 model(1.0f);
  model = glm::translate(model, position);
  model = model * transform->GetRotationMatrix();
  record.world_ = model;

  float opacity = 1.0f;
  float roughness = 0.5f;
  float metallic = 0.0f;
  glm::vec2 tiling(1.0f);
  if (auto* parent_mesh = registry.try_get<Mesh>(parent_mesh_entity)) {
    opacity = parent_mesh->opacity_;
    roughness = parent_mesh->roughness_;
    metallic = parent_mesh->metallic_;
    tiling = parent_mesh->tiling_;
  }
  record.opacity = opacity;
  record.material_params = glm::vec4(roughness, metallic, tiling);

  glm::vec4 atlas_uv = atlas.GetWhitePixelUV();
  if (auto* tex = registry.try_get<Texture2D>(entity)) {
    atlas_uv = tex->GetAtlasUV();
  } else if (auto* ref = registry.try_get<TextureRef>(entity);
             ref && ref->texture) {
    atlas_uv = ref->texture->GetAtlasUV();
  } else if (entity != parent_mesh_entity) {
    if (auto* parent_tex = registry.try_get<Texture2D>(parent_mesh_entity)) {
      atlas_uv = parent_tex->GetAtlasUV();
    }
  }
  record.atlas_uv = atlas_uv;
  return record;
}

void WebGpuApplication::EnsureRegistrySignalsHooked(entt::registry& registry) {
  if (hooked_registry_ == &registry) return;
  registry.on_construct<Mesh>().connect<&WebGpuApplication::OnLayoutChanged>(
      *this);
  registry.on_destroy<Mesh>().connect<&WebGpuApplication::OnLayoutChanged>(
      *this);
  registry.on_construct<MeshInstance>()
      .connect<&WebGpuApplication::OnLayoutChanged>(*this);
  registry.on_destroy<MeshInstance>()
      .connect<&WebGpuApplication::OnLayoutChanged>(*this);
  registry.on_construct<Transform>()
      .connect<&WebGpuApplication::OnLayoutChanged>(*this);
  registry.on_destroy<Transform>().connect<&WebGpuApplication::OnLayoutChanged>(
      *this);
  hooked_registry_ = &registry;
  layout_dirty_ = true;
}

void WebGpuApplication::RebuildInstanceLayout(entt::registry& registry,
                                              TextureAtlas& atlas) {
  mesh_layout_.clear();
  mesh_order_.clear();
  entity_slot_.clear();
  slot_owner_.clear();
  slot_parent_.clear();

  instance_cpu_cache_.clear();
  instance_cpu_cache_.reserve(64);

  UINT running_index = 0;
  auto mesh_view = registry.view<Mesh>();
  for (auto [entity, mesh] : mesh_view.each()) {
    MeshLayoutEntry entry;
    entry.index_count = static_cast<UINT>(mesh.indices_.size());
    entry.start_index = running_index;
    entry.start_instance = static_cast<UINT>(instance_cpu_cache_.size());
    entry.instance_count = 0;

    if (registry.try_get<Transform>(entity)) {
      UINT slot = static_cast<UINT>(instance_cpu_cache_.size());
      instance_cpu_cache_.push_back(
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

    UINT insert_slot =
        layout_it->second.start_instance + layout_it->second.instance_count;
    instance_cpu_cache_.insert(
        instance_cpu_cache_.begin() + insert_slot,
        BuildInstanceRecord(registry, atlas, entity, parent));
    slot_owner_.insert(slot_owner_.begin() + insert_slot, entity);
    slot_parent_.insert(slot_parent_.begin() + insert_slot, parent);
    layout_it->second.instance_count += 1;

    for (auto& kv : mesh_layout_) {
      if (kv.first != parent && kv.second.start_instance >= insert_slot) {
        kv.second.start_instance += 1;
      }
    }
    for (auto& kv : entity_slot_) {
      if (kv.second >= insert_slot) kv.second += 1;
    }
    entity_slot_.emplace(entity, insert_slot);
  }

  layout_dirty_ = false;
}

void WebGpuApplication::UpdateDirtyInstances(entt::registry& registry,
                                             TextureAtlas& atlas) {
  for (size_t slot = 0; slot < instance_cpu_cache_.size(); ++slot) {
    entt::entity entity = slot_owner_[slot];
    entt::entity parent = slot_parent_[slot];
    if (registry.valid(entity)) {
      instance_cpu_cache_[slot] =
          BuildInstanceRecord(registry, atlas, entity, parent);
    }
  }
}

void WebGpuApplication::OnNewTick() {
  if (!device_ready_ || !game_scene_) return;

  vertex_buffer_cpu_.clear();
  index_buffer_cpu_.clear();

  auto& registry = game_scene_->GetRegistry();
  auto mesh_view = registry.view<Mesh>();
  for (auto [entity, mesh] : mesh_view.each()) {
    const auto offset = static_cast<UINT>(vertex_buffer_cpu_.size());
    for (UINT index : mesh.indices_) {
      index_buffer_cpu_.push_back(index + offset);
    }
    vertex_buffer_cpu_.insert(vertex_buffer_cpu_.end(), mesh.vertices_.begin(),
                              mesh.vertices_.end());
  }
}

void WebGpuApplication::OnResize(int32_t width, int32_t height) {
  Application::OnResize(width, height);
  if (device_ready_) {
    ConfigureSurface(width, height);
  }
}

void WebGpuApplication::ImGuiNewFrame() {
  if (device_ready_) {
    ImGui_ImplWGPU_NewFrame();
  }
}

void WebGpuApplication::Render() {
  if (!device_ready_ || !game_scene_) {
    ImGui::EndFrame();
    return;
  }

  // 1. Camera & Lighting parameters
  glm::mat4 camera_proj_view(1.0f);
  if (game_scene_->GetCameraProj(camera_proj_view)) {
    camera_.mWorldViewProj = camera_proj_view;
    camera_.mResolution =
        PQ_FLOAT2{game_scene_->GetWidth(), game_scene_->GetHeight()};
  }

  const SceneLighting& lighting = game_scene_->GetLighting();
  camera_.mLightDirection = PQ_FLOAT4(lighting.direction, 0.0f);
  camera_.mLightColor = PQ_FLOAT4(lighting.color, 1.0f);
  camera_.mAmbientColor = PQ_FLOAT4(lighting.ambient, 1.0f);
  glm::vec3 camera_pos(0.0f);
  game_scene_->GetCameraPosition(camera_pos);
  camera_.mCameraPosition = PQ_FLOAT4(camera_pos, 1.0f);

  if (lighting.cast_shadows) {
    glm::vec3 light_dir = glm::normalize(lighting.direction);
    if (glm::length(light_dir) < 0.001f) {
      light_dir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    }
    float shadow_dist =
        lighting.shadow_distance > 0.0f ? lighting.shadow_distance : 60.0f;
    glm::vec3 light_target = camera_pos;
    glm::vec3 light_pos = light_target - light_dir * shadow_dist;
    glm::vec3 up = std::abs(light_dir.y) > 0.99f
                       ? glm::vec3(0.0f, 0.0f, 1.0f)
                       : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 light_view = glm::lookAt(light_pos, light_target, up);

    float half_box = shadow_dist * 0.6f;
    glm::mat4 light_proj = glm::ortho(
        -half_box, half_box, -half_box, half_box, 1.0f, shadow_dist * 2.0f);

    // Texel snapping in light space
    glm::vec4 shadow_origin = light_view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float texel_size =
        (2.0f * half_box) / static_cast<float>(kShadowMapResolution);
    float snap_x = std::fmod(shadow_origin.x, texel_size);
    float snap_y = std::fmod(shadow_origin.y, texel_size);
    light_view[3][0] -= snap_x;
    light_view[3][1] -= snap_y;

    glm::mat4 light_view_proj = light_proj * light_view;
    camera_.mLightViewProj = light_view_proj;
    camera_.mShadowParams = PQ_FLOAT2{
        static_cast<float>(kShadowMapResolution), lighting.shadow_normal_bias};

    queue_.WriteBuffer(shadow_ubo_, 0, &light_view_proj, sizeof(glm::mat4));
  } else {
    camera_.mLightViewProj = glm::mat4(1.0f);
    camera_.mShadowParams = PQ_FLOAT2{-1.0f, 0.0f};
    glm::mat4 identity(1.0f);
    queue_.WriteBuffer(shadow_ubo_, 0, &identity, sizeof(glm::mat4));
  }

  queue_.WriteBuffer(camera_ubo_, 0, &camera_, sizeof(CameraCBuffer));

  // 2. Atlas upload
  TextureAtlas& atlas = game_scene_->GetAtlas();
  if (atlas.NeedsGpuUpload()) {
    UpdateAtlasTexture();
  }

  // 3. Instance buffer update
  auto& registry = game_scene_->GetRegistry();
  EnsureRegistrySignalsHooked(registry);

  if (layout_dirty_) {
    RebuildInstanceLayout(registry, atlas);
  } else {
    UpdateDirtyInstances(registry, atlas);
  }

  if (!instance_cpu_cache_.empty()) {
    queue_.WriteBuffer(instance_buffer_, 0, instance_cpu_cache_.data(),
                       sizeof(VsModelBuffer) *
                           std::min(instance_cpu_cache_.size(), kMaxInstances));
  }

  if (!vertex_buffer_cpu_.empty()) {
    queue_.WriteBuffer(vertex_buffer_, 0, vertex_buffer_cpu_.data(),
                       sizeof(Vertex) *
                           std::min(vertex_buffer_cpu_.size(), kMaxVertices));
  }

  if (!index_buffer_cpu_.empty()) {
    queue_.WriteBuffer(index_buffer_, 0, index_buffer_cpu_.data(),
                       sizeof(UINT) *
                           std::min(index_buffer_cpu_.size(), kMaxIndices));
  }

  // Static buffers
  auto static_verts = game_scene_->GetStaticVertices();
  auto static_inds = game_scene_->GetStaticIndices();
  if (!static_verts.empty()) {
    queue_.WriteBuffer(static_vertex_buffer_, 0, static_verts.data(),
                       sizeof(StaticVertex) * static_verts.size());
  }
  if (!static_inds.empty()) {
    queue_.WriteBuffer(static_index_buffer_, 0, static_inds.data(),
                       sizeof(UINT) * static_inds.size());
  }

  // 4. Command encoding
  wgpu::SurfaceTexture surface_texture;
  surface_.GetCurrentTexture(&surface_texture);
  if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
      surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
    ImGui::EndFrame();
    return;
  }
  auto current_view = surface_texture.texture.CreateView();

  wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();

  if (lighting.cast_shadows) {
    RecordShadowPass(encoder);
  }

  RecordScenePass(encoder, current_view);

  wgpu::CommandBuffer cmd = encoder.Finish();
  queue_.Submit(1, &cmd);
}

void WebGpuApplication::RecordShadowPass(wgpu::CommandEncoder& encoder) {
  wgpu::RenderPassDepthStencilAttachment depth_att = {};
  depth_att.view = shadow_view_;
  depth_att.depthClearValue = 1.0f;
  depth_att.depthLoadOp = wgpu::LoadOp::Clear;
  depth_att.depthStoreOp = wgpu::StoreOp::Store;

  wgpu::RenderPassDescriptor pass_desc = {};
  pass_desc.depthStencilAttachment = &depth_att;

  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);
  pass.SetPipeline(shadow_pipeline_);
  pass.SetBindGroup(0, shadow_bind_group_);
  pass.SetVertexBuffer(0, vertex_buffer_, 0, sizeof(Vertex) * kMaxVertices);
  pass.SetVertexBuffer(1, instance_buffer_, 0,
                       sizeof(VsModelBuffer) * kMaxInstances);
  pass.SetIndexBuffer(index_buffer_, wgpu::IndexFormat::Uint32, 0,
                      sizeof(UINT) * kMaxIndices);

  for (auto entity : mesh_order_) {
    const auto& entry = mesh_layout_[entity];
    if (entry.instance_count == 0) continue;
    if (entry.start_instance >= kMaxInstances) continue;
    UINT count =
        std::min(entry.instance_count, static_cast<UINT>(kMaxInstances) - entry.start_instance);
    pass.DrawIndexed(entry.index_count, count, entry.start_index, 0,
                     entry.start_instance);
  }

  pass.End();
}

void WebGpuApplication::RecordScenePass(wgpu::CommandEncoder& encoder,
                                        wgpu::TextureView& current_view) {
  wgpu::RenderPassColorAttachment color_att = {};
  color_att.view = current_view;
  color_att.loadOp = wgpu::LoadOp::Clear;
  color_att.storeOp = wgpu::StoreOp::Store;
  const glm::vec4 clear = game_scene_->GetClearColor();
  color_att.clearValue = {clear.r, clear.g, clear.b, clear.a};

  wgpu::RenderPassDepthStencilAttachment depth_att = {};
  depth_att.view = depth_view_;
  depth_att.depthClearValue = 1.0f;
  depth_att.depthLoadOp = wgpu::LoadOp::Clear;
  depth_att.depthStoreOp = wgpu::StoreOp::Discard;

  wgpu::RenderPassDescriptor pass_desc = {};
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &color_att;
  pass_desc.depthStencilAttachment = &depth_att;

  UpdateSkyboxTexture();
  glm::mat4 sky_matrix;
  const bool draw_skybox =
      skybox_bind_group_ && game_scene_->GetSkyboxMatrix(sky_matrix);
  if (draw_skybox) {
    queue_.WriteBuffer(skybox_ubo_, 0, &sky_matrix, sizeof(sky_matrix));
  }

  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);

  // Draw Skybox
  if (draw_skybox) {
    pass.SetPipeline(skybox_pipeline_);
    pass.SetBindGroup(0, skybox_bind_group_);
    pass.Draw(3);
  }

  // Draw Static Geometry
  auto static_inds = game_scene_->GetStaticIndices();
  if (!static_inds.empty()) {
    pass.SetPipeline(static_pipeline_);
    pass.SetBindGroup(0, main_bind_group_);
    pass.SetVertexBuffer(0, static_vertex_buffer_);
    pass.SetIndexBuffer(static_index_buffer_, wgpu::IndexFormat::Uint32);
    pass.DrawIndexed(static_cast<uint32_t>(static_inds.size()), 1, 0, 0, 0);
  }

  // Draw Instanced Meshes
  if (!mesh_order_.empty()) {
    pass.SetPipeline(instanced_pipeline_);
    pass.SetBindGroup(0, main_bind_group_);
    pass.SetVertexBuffer(0, vertex_buffer_, 0, sizeof(Vertex) * kMaxVertices);
    pass.SetVertexBuffer(1, instance_buffer_, 0,
                         sizeof(VsModelBuffer) * kMaxInstances);
    pass.SetIndexBuffer(index_buffer_, wgpu::IndexFormat::Uint32, 0,
                        sizeof(UINT) * kMaxIndices);

    for (auto entity : mesh_order_) {
      const auto& entry = mesh_layout_[entity];
      if (entry.instance_count == 0) continue;
      if (entry.start_instance >= kMaxInstances) continue;
      UINT count =
          std::min(entry.instance_count, static_cast<UINT>(kMaxInstances) - entry.start_instance);
      pass.DrawIndexed(entry.index_count, count, entry.start_index, 0,
                       entry.start_instance);
    }
  }

  // Draw ImGui overlay
  ImGui::Render();
  ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());

  pass.End();
}

}  // namespace Mercury
