// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera uniform buffer         (vertex stage)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

Texture2D<float4> diffuse_texture : register(t0);
SamplerState texture_sampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

PSOutput Main(PSInput input)
{
    PSOutput output = (PSOutput) 0;
    output.color = diffuse_texture.Sample(texture_sampler, input.uv) * input.color;
    return output;
}
