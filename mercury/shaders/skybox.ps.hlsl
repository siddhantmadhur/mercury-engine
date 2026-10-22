// Vulkan resource mapping (skybox descriptor set 0). DXC derives the bindings
// from the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   t0 -> binding 10  equirectangular panorama (fragment stage)
//   s0 -> binding 20  panorama sampler         (fragment stage)
//   push constant     inverse projection * rotation-only view
// Keep this in sync with CreateSkyboxResources() in vulkan_application.cc.

struct SkyboxConstants
{
    matrix mInvViewProj;
};
[[vk::push_constant]] SkyboxConstants skybox;

Texture2D<float4> panorama         : register(t0);
SamplerState      panorama_sampler : register(s0);

static const float kPi = 3.14159265;

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

float4 Main(PSInput input) : SV_Target0
{
    float4 far_point = mul(skybox.mInvViewProj, float4(input.ndc, 1.0, 1.0));
    float3 dir = normalize(far_point.xyz / far_point.w);

    // Longitude around +Y (the image centre faces -Z), latitude from the top.
    float2 uv;
    uv.x = atan2(dir.x, -dir.z) / (2.0 * kPi) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / kPi;
    // Explicit LOD: the derivative of uv.x jumps across the seam.
    return float4(panorama.SampleLevel(panorama_sampler, uv, 0).rgb, 1.0);
}
