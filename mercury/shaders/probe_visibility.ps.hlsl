// The distances a light probe captured, filtered into its octahedral
// visibility map: per direction the mean and mean square of the distance to
// the nearest surface, so shading can tell when a wall stands between the
// probe and a point (Chebyshev's inequality, as in DDGI). Drawn over the
// probe's kProbeVisibilitySize^2 texels.
// Uses the fullscreen passes' layout (post_common.hlsli): binding 2 is the
// capture, push constants are VisibilityPush.
#include "lighting_math.hlsli"

struct FullscreenOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(2, 0)]] Texture2DArray<float4> faces;

struct VisibilityPush
{
    float4 probe;  // x: first face layer, y: face size, zw: atlas texel of the map's corner
    float4 limits; // x: distance stored for open sky (and the most stored)
};
[[vk::push_constant]] VisibilityPush push;

float4 Main(FullscreenOutput input) : SV_Target0
{
    float2 texel = input.position.xy - push.probe.zw;  // pixel centers: x.5
    float3 direction = OctDecode(texel / kProbeVisibilitySize * 2.0 - 1.0);
    const uint size = (uint) push.probe.y;
    const uint first = (uint) push.probe.x;
    const float max_distance = push.limits.x;
    float mean = 0.0;
    float mean_sq = 0.0;
    float weight = 0.0;
    for (uint f = 0; f < 6; ++f)
    {
        for (uint y = 0; y < size; ++y)
        {
            for (uint x = 0; x < size; ++x)
            {
                float2 uv = (float2(x, y) + 0.5) / size;
                float3 d = CubeFaceDirection(uv, f);
                // A narrow lobe: the distance is sharp across the map.
                float w = pow(saturate(dot(d, direction)), 40.0);
                if (w <= 1e-4) continue;
                float a = faces.Load(int4(x, y, first + f, 0)).a;
                float distance = a > kProbeSkyAlpha ? max_distance : min(abs(a), max_distance);
                mean += distance * w;
                mean_sq += distance * distance * w;
                weight += w;
            }
        }
    }
    if (weight <= 0.0) return float4(max_distance, max_distance * max_distance, 0.0, 1.0);
    return float4(mean / weight, mean_sq / weight, 0.0, 1.0);
}
