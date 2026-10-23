// Shared by the model renderer's shaders (vk_scene_renderer.cc).
//
// Descriptor set 0 (per camera pass):
//   binding 0  FrameUniforms         dynamic uniform buffer
//   binding 1  DrawData[]            one record per draw
//   binding 2  float4x4[]            bone matrices of skinned draws
//   binding 3  shadow map            depth image
//   binding 4  shadow sampler        comparison
//   binding 5  material sampler      repeat, trilinear (anisotropic)
//   binding 6  clamp sampler         for camera RenderTextures
// Descriptor set 1:
//   binding 0  Texture2D[]           every material texture (bindless)
// Keep in step with FrameUniforms/DrawData in vk_scene_renderer.h.

[[vk::binding(0, 0)]] cbuffer FrameUniforms
{
    float4x4 view_proj;
    float4x4 view;
    float4x4 proj;
    float4x4 inv_view_proj;
    float4x4 light_view_proj;
    float4 light_dir;       // xyz: the way the light travels
    float4 light_color;     // rgb: linear color * intensity
    float4 ambient_sky;     // rgb: linear
    float4 ambient_ground;  // rgb: linear
    float4 camera_pos;
    float4 resolution;      // xy: target size, zw: 1 / size
    float4 shadow_params;   // x: shadow map size (0: none), y: normal bias, z: depth bias
    float4 flags;           // x: tonemap in shader, y: overlay pass, z: time
};

struct DrawData
{
    float4x4 world;
    float4x4 normal_matrix;
    float4 color;     // linear rgb, alpha
    float4 emission;  // linear rgb
    float4 uv;        // xy: tiling, zw: offset
    uint4 info;       // x: texture index, y: flags, z: first bone (~0: rigid)
    float4 material;  // x: metallic, y: smoothness
};

static const uint kFlagUnlit = 1;
static const uint kFlagSdfText = 2;
static const uint kFlagClampTexture = 4;
static const uint kFlagTransparent = 8;
static const uint kNoSkin = 0xffffffff;

[[vk::binding(1, 0)]] StructuredBuffer<DrawData> draws;
[[vk::binding(2, 0)]] StructuredBuffer<float4x4> bones;

struct ModelVSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] uint4 joints : BLENDINDICES;
    [[vk::location(5)]] float4 weights : BLENDWEIGHT;
    // Per instance: which DrawData this draw uses (firstInstance).
    [[vk::location(6)]] uint draw : DRAWINDEX;
};

struct ModelVSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world_position : POSITION1;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] nointerpolation uint draw : DRAWINDEX;
};

// Skinned position and normal in the draw's (entity) space.
void SkinVertex(ModelVSInput input, DrawData data, out float4 position, out float3 normal)
{
    position = float4(input.position, 1.0);
    normal = input.normal;
    if (data.info.z == kNoSkin) return;
    float4x4 skin = bones[data.info.z + input.joints.x] * input.weights.x
                  + bones[data.info.z + input.joints.y] * input.weights.y
                  + bones[data.info.z + input.joints.z] * input.weights.z
                  + bones[data.info.z + input.joints.w] * input.weights.w;
    position = mul(skin, position);
    normal = mul((float3x3) skin, normal);
}

float3 SrgbToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}
