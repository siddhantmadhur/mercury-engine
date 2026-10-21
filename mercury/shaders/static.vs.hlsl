// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera uniform buffer         (vertex stage)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

// Static vertex shader: positions are pre-baked into world space and atlas
// UVs ride per-vertex, so no per-object cbuffer is needed.
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

struct VSInput
{
    // Vertex buffer binding 0, layout matches StaticVertex
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 atlas_uv : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

VSOutput Main(VSInput input)
{
    // Snap in world space (before WVP) so exact half-integer coords like
    // ±638.5 don't get perturbed by the projection roundtrip and end up
    // rounding asymmetrically across the origin. Round-half-toward-zero
    // keeps odd-thickness primitives at their intended pixel width.
    float3 snapped = input.position;
    //snapped.xy = sign(snapped.xy) * ceil(abs(snapped.xy) - 0.5);

    VSOutput output = (VSOutput) 0;
    output.position = mul(mWorldViewProj, float4(snapped, 1.0));

    output.color = input.color;
    output.uv = input.atlas_uv.xy + input.uv * (input.atlas_uv.zw - input.atlas_uv.xy);
    return output;
}
