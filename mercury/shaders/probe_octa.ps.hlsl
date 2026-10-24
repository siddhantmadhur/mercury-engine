// Turns a light probe's L2 spherical harmonics into two small octahedral
// maps that shading reads with one bilinear fetch per direction instead of
// evaluating the 9 coefficients (lighting.hlsli):
//   target 0  irradiance / pi toward each direction (the cosine lobe's
//             bands), alpha the probe's validity
//   target 1  the sharpest radiance L2 holds without ringing (Hann bands),
//             for reflections
// Drawn over the probe's kProbeOctaTexels^2 texels of both atlases: the
// kProbeOctaSize^2 interior plus a one-texel border folded across the
// octahedron's edges, so bilinear filtering is seamless (as DDGI).
// Uses the fullscreen passes' layout (post_common.hlsli): binding 2 is the
// coefficient atlas, push constants are OctaPush.
#include "lighting_math.hlsli"

struct FullscreenOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct OctaOutput
{
    float4 irradiance : SV_Target0;
    float4 radiance : SV_Target1;
};

[[vk::binding(2, 0)]] Texture2D<float4> coefficients;

struct OctaPush
{
    float4 probe;  // xy: atlas texel of coefficient 0, zw: corner of the probe's maps
};
[[vk::push_constant]] OctaPush push;

OctaOutput Main(FullscreenOutput input)
{
    float2 texel = input.position.xy - push.probe.zw - 1.0;  // interior from 0
    float3 d = OctDecode(OctFold(texel / kProbeOctaSize * 2.0 - 1.0));
    float basis[9];
    ShBasis(d, basis);
    int2 base = int2(push.probe.xy);
    float4 c0 = coefficients.Load(int3(base, 0));
    float3 irradiance = c0.rgb * (kCosineBands.x * basis[0]);
    float3 radiance = c0.rgb * (kSharpBands.x * basis[0]);
    [unroll]
    for (int i = 1; i < 9; ++i)
    {
        float3 c = coefficients.Load(int3(base + int2(i, 0), 0)).rgb;
        irradiance += c * ((i < 4 ? kCosineBands.y : kCosineBands.z) * basis[i]);
        radiance += c * ((i < 4 ? kSharpBands.y : kSharpBands.z) * basis[i]);
    }
    OctaOutput output;
    output.irradiance = float4(max(irradiance, 0.0), c0.a);
    output.radiance = float4(max(radiance, 0.0), 1.0);
    return output;
}
