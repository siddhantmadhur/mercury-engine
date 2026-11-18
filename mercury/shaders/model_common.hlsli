// Shared by the basic model renderer's shaders (vk_scene_renderer.cc).
//
// Descriptor set 0 (per camera pass):
//   binding 0  FrameUniforms         dynamic uniform buffer
//   binding 1  DrawData[]            one record per draw
//   binding 2  float4x4[]            bone matrices of skinned draws
//   binding 3  material sampler      repeat, trilinear (anisotropic)
//   binding 4  clamp sampler         for camera RenderTextures
// Descriptor set 1:
//   binding 0  Texture2D[]           every material texture (bindless)
// Keep in step with FrameUniforms/DrawData in vk_scene_renderer.h.

[[vk::binding(0, 0)]] cbuffer FrameUniforms
{
    float4x4 view_proj;
    float4 flags;  // x > 0.5: encode linear -> sRGB on output
};

struct DrawData
{
    float4x4 world;
    float4 color;  // linear rgb, alpha
    float4 uv;     // xy: tiling, zw: offset
    uint4 info;    // x: texture index, y: flags, z: first bone (~0: rigid)
};

static const uint kFlagSdfText = 2;
static const uint kFlagClampTexture = 4;
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
    [[vk::location(6)]] uint draw : DRAWINDEX;
    [[vk::location(7)]] float4 tangent : TANGENT;
};

struct ModelVSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation uint draw : DRAWINDEX;
};

float4 SkinPosition(ModelVSInput input, DrawData data)
{
    float4 position = float4(input.position, 1.0);
    if (data.info.z == kNoSkin) return position;
    float4x4 skin = bones[data.info.z + input.joints.x] * input.weights.x
                  + bones[data.info.z + input.joints.y] * input.weights.y
                  + bones[data.info.z + input.joints.z] * input.weights.z
                  + bones[data.info.z + input.joints.w] * input.weights.w;
    return mul(skin, position);
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}
