// Static vertex shader: positions are pre-baked into world space and atlas
// UVs ride per-vertex. See main.vs.hlsl for the descriptor set mapping.

cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
};

struct VSInput
{
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
    [[vk::location(2)]] nointerpolation float4 atlas_rect : TEXCOORD1;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.position = mul(mWorldViewProj, float4(input.position, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    output.atlas_rect = input.atlas_uv;
    return output;
}
