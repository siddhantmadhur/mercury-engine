// Basic unlit atlas-textured fragment shader. See main.vs.hlsl for bindings.

Texture2D<float4> diffuse_texture : register(t0);
SamplerState      texture_sampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float4 atlas_rect : TEXCOORD1;
};

float4 Main(PSInput input) : SV_Target0
{
    float2 atlas_uv = input.atlas_rect.xy
                    + frac(input.uv) * (input.atlas_rect.zw - input.atlas_rect.xy);
    return diffuse_texture.Sample(texture_sampler, atlas_uv) * input.color;
}
