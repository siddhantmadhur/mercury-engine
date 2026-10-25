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
//   binding 12 sun transmittance     rgb: light let through by transmissive
//                                    casters, a: the nearest one's depth
//   binding 13 cube transmittance    the same per shadow cube, a: distance
//                                    over the cube's far distance
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
    float4 flags;           // x: tonemap in shader, y: overlay pass, z: time, w: point lights
    // Per point light: [2i] xyz position, w range; [2i + 1] rgb linear color
    // * intensity, w shadow cube slot (-1: none).
    float4 point_lights[2 * 16];
    float4 point_shadows[16];   // x: light radius, y: shadow bias, z: soft (1) or hard
    // Per area light: [4i] center, range; [4i + 1] half the width, two-sided;
    // [4i + 2] half the height, shadow slot (-1: none); [4i + 3] radiance, soft.
    float4 area_lights[4 * 4];
    float4 area_shadow_bias[4]; // x: bias
    float4 shadow_cubes_info[8]; // per shadow slot: xyz cube center, w far distance
    float4 light_info;          // x: area lights, y: probe capture pass, z: cube size, w: capture emission
    float4 gi_min;              // xyz: probe volume min, w: enabled
    float4 gi_max;              // xyz: probe volume max, w: intensity
    float4 gi_counts;           // xyz: probes along each axis, w: probes per atlas row
    float4x4 far_light_view_proj; // the sun's shadow over the whole scene
    float4 far_shadow_params;     // x: resolution (0: none), y: normal bias, w: use it alone
    float4 gi_params;             // x: the visibility maps filter linearly
    // x: the scene behind transmissive surfaces (a texture slot: rgb color,
    // a view distance; 0: none), y: its mips. z: transmissive casters in the
    // sun's map (1), w: in the shadow cubes (1).
    float4 transmission_params;
};

static const uint kMaxPointLights = 16;
static const uint kMaxAreaLights = 4;

struct DrawData
{
    float4x4 world;
    float4x4 normal_matrix;
    float4 color;     // linear rgb, alpha
    float4 emission;  // linear rgb
    float4 uv;        // xy: tiling, zw: offset
    uint4 info;       // x: texture index, y: flags, z: first bone (~0: rigid)
    float4 material;  // x: metallic, y: smoothness, z: normal map strength
    uint4 maps;       // x: normal map index, y: emission map index (0: none)
    float4 transmission;  // rgb: tint (linear), w: share of light passing through
    float4 volume;        // x: index of refraction, y: thickness (0: thin), z: size,
                          // w: roundness (1: a ball, 0: a sheet)
};

static const uint kFlagUnlit = 1;
static const uint kFlagSdfText = 2;
static const uint kFlagClampTexture = 4;
static const uint kFlagTransparent = 8;
static const uint kFlagTransmissive = 16;
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
    [[vk::location(7)]] float4 tangent : TANGENT;  // w: bitangent sign
};

// The vertex shader's other outputs; SV_Position is its own (Invariant).
struct ModelVSOutput
{
    [[vk::location(0)]] float3 world_position : POSITION1;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(4)]] nointerpolation uint draw : DRAWINDEX;
    [[vk::location(5)]] float4 tangent : TANGENT;  // world, w: bitangent sign
};

// Skinned position, normal and tangent in the draw's (entity) space.
void SkinVertex(ModelVSInput input, DrawData data, out float4 position, out float3 normal,
                out float3 tangent)
{
    position = float4(input.position, 1.0);
    normal = input.normal;
    tangent = input.tangent.xyz;
    if (data.info.z == kNoSkin) return;
    float4x4 skin = bones[data.info.z + input.joints.x] * input.weights.x
                  + bones[data.info.z + input.joints.y] * input.weights.y
                  + bones[data.info.z + input.joints.z] * input.weights.z
                  + bones[data.info.z + input.joints.w] * input.weights.w;
    position = mul(skin, position);
    normal = mul((float3x3) skin, normal);
    tangent = mul((float3x3) skin, tangent);
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
