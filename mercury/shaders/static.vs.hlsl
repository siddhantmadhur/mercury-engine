// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera and light uniform buffer (vertex + fragment)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   t1 -> binding 11  shadow map depth image        (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
//   s1 -> binding 21  shadow sampler                (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

// Static vertex shader: positions are pre-baked into world space and atlas
// UVs ride per-vertex, so no per-object cbuffer is needed.
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
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float3 world_position : POSITION1;
    [[vk::location(4)]] float2 material_params : TEXCOORD1;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.position = mul(mWorldViewProj, float4(input.position, 1.0));

    output.color = input.color;
    output.normal = float3(0.0, 0.0, 0.0);  // static geometry is unlit
    output.world_position = input.position;
    output.uv = input.atlas_uv.xy + input.uv * (input.atlas_uv.zw - input.atlas_uv.xy);
    output.material_params = float2(0.5, 0.0);
    return output;
}
