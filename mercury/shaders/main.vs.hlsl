// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera and light uniform buffer (vertex + fragment)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   t1 -> binding 11  shadow map depth image        (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
//   s1 -> binding 21  shadow sampler                (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    matrix mLightViewProj;
    float4 mLightDirection;  // xyz: the way the light travels
    float4 mLightColor;      // rgb: color * intensity
    float4 mAmbientColor;
    float4 mCameraPosition;
    float2 mResolution;
    float2 mShadowParams;    // x: shadow map resolution, y: normal bias
};

struct VSInput
{
    // Per-vertex data (vertex buffer binding 0, layout matches Vertex)
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;

    // Per-instance data (vertex buffer binding 1, layout matches VsModelBuffer)
    [[vk::location(3)]] float3 inst_scale     : INSTANCE_SCALE;
    [[vk::location(4)]] float  inst_opacity   : INSTANCE_OPACITY;
    [[vk::location(5)]] float4 inst_world0    : INSTANCE_WORLD0;
    [[vk::location(6)]] float4 inst_world1    : INSTANCE_WORLD1;
    [[vk::location(7)]] float4 inst_world2    : INSTANCE_WORLD2;
    [[vk::location(8)]] float4 inst_world3    : INSTANCE_WORLD3;
    [[vk::location(9)]] float4 inst_atlas_uv  : INSTANCE_ATLAS_UV;

    // Zero for unlit meshes (sprites); see Vertex in globals.h
    [[vk::location(10)]] float3 normal : NORMAL;

    // Material parameters (roughness, metallic, tiling columns, tiling rows)
    [[vk::location(11)]] float4 inst_material : INSTANCE_MATERIAL;
};

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float3 world_position : POSITION1;
    [[vk::location(4)]] float2 material_params : TEXCOORD1;
    // The texture's rectangle in the atlas (x0, y0, x1, y1); uv repeats
    // inside it, so tiling never bleeds into neighbouring images.
    [[vk::location(5)]] nointerpolation float4 atlas_rect : TEXCOORD2;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    // Reconstruct the per-instance world matrix from four float4 inputs.
    matrix world = matrix(input.inst_world0,
                          input.inst_world1,
                          input.inst_world2,
                          input.inst_world3);

    float3 scaled_position = input.position * input.inst_scale;
    float4 world_pos = mul(float4(scaled_position, 1.0), world);

    output.position = mul(mWorldViewProj, world_pos);
    output.world_position = world_pos.xyz;

    // Normals use the inverse scale so they stay perpendicular under
    // non-uniform scaling; the rotation comes from the world matrix.
    float3 tiny = (float3)(abs(input.inst_scale) < 1e-6) * 1e-6;
    float3 scaled_normal = input.normal / (input.inst_scale + tiny);
    output.normal = mul(float4(scaled_normal, 0.0), world).xyz;

    output.color = float4(input.color, input.inst_opacity);
    output.uv = input.uv * input.inst_material.zw;
    output.atlas_rect = input.inst_atlas_uv;
    output.material_params = input.inst_material.xy;
    return output;
}
