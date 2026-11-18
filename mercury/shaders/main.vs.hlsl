// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in tools/shader.bzl:
//   b0 -> binding 0   camera uniform buffer (vertex stage)
//   t0 -> binding 10  atlas sampled image   (fragment stage)
//   s0 -> binding 20  atlas sampler         (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
};

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;

    [[vk::location(3)]] float3 inst_scale     : INSTANCE_SCALE;
    [[vk::location(4)]] float  inst_opacity   : INSTANCE_OPACITY;
    [[vk::location(5)]] float4 inst_world0    : INSTANCE_WORLD0;
    [[vk::location(6)]] float4 inst_world1    : INSTANCE_WORLD1;
    [[vk::location(7)]] float4 inst_world2    : INSTANCE_WORLD2;
    [[vk::location(8)]] float4 inst_world3    : INSTANCE_WORLD3;
    [[vk::location(9)]] float4 inst_atlas_uv  : INSTANCE_ATLAS_UV;

    [[vk::location(10)]] float3 normal        : NORMAL;
    [[vk::location(11)]] float4 inst_material : INSTANCE_MATERIAL;
};

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float4 atlas_rect : TEXCOORD1;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    matrix world = matrix(input.inst_world0,
                          input.inst_world1,
                          input.inst_world2,
                          input.inst_world3);
    float3 scaled_position = input.position * input.inst_scale;
    float4 world_pos = mul(float4(scaled_position, 1.0), world);

    output.position = mul(mWorldViewProj, world_pos);
    output.color = float4(input.color, input.inst_opacity);
    output.uv = input.uv * input.inst_material.zw;
    output.atlas_rect = input.inst_atlas_uv;
    return output;
}
