// Projects the 6 faces a light probe captured onto L2 spherical harmonics.
// Drawn over the probe's 9 texels of the atlas (one per coefficient). Alpha
// of coefficient 0 says whether the probe is usable: probes that mostly see
// back faces are inside something. Texels that saw nothing take the ambient
// sky and ground colors by direction.
// Uses the fullscreen passes' layout (post_common.hlsli): binding 2 is the
// capture, push constants are ProbePush.
#include "lighting_math.hlsli"

struct FullscreenOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[[vk::binding(2, 0)]] Texture2DArray<float4> faces;

struct ProbePush
{
    float4 probe;  // x: first face layer, y: face size, zw: atlas texel of coefficient 0
    float4 limits; // x: fraction of back faces above which the probe is unusable
    float4 sky;    // ambient light from above, where a face saw nothing
    float4 ground; // and from below
};
[[vk::push_constant]] ProbePush push;

float4 Main(FullscreenOutput input) : SV_Target0
{
    const uint coefficient = (uint) (input.position.x - push.probe.z);
    const uint size = (uint) push.probe.y;
    const uint first = (uint) push.probe.x;
    float3 sum = 0.0;
    float weight = 0.0;
    float back = 0.0;
    for (uint f = 0; f < 6; ++f)
    {
        for (uint y = 0; y < size; ++y)
        {
            for (uint x = 0; x < size; ++x)
            {
                float2 uv = (float2(x, y) + 0.5) / size;
                float3 d = CubeFaceDirection(uv, f);
                // Solid angle of a cube-map texel.
                float2 ndc = uv * 2.0 - 1.0;
                float w = 1.0 / pow(1.0 + dot(ndc, ndc), 1.5);
                float4 texel = faces.Load(int4(x, y, first + f, 0));
                if (texel.a > 1.5)
                {
                    // Nothing there (alpha 2): the sky, as the ambient colors.
                    texel = float4(lerp(push.ground.rgb, push.sky.rgb, d.y * 0.5 + 0.5), 1.0);
                }
                float basis[9];
                ShBasis(d, basis);
                sum += texel.rgb * basis[coefficient] * w;
                weight += w;
                back += (1.0 - texel.a) * w;
            }
        }
    }
    // The weights add up to the sphere's 4 pi steradians.
    float3 value = sum * (4.0 * LIGHTING_PI / weight);
    float valid = back / weight < push.limits.x ? 1.0 : 0.0;
    return float4(value, coefficient == 0 ? valid : 1.0);
}
