// Background: a 2:1 panorama (input0) when p4.x > 0.5, otherwise Unity's
// default procedural sky. p0..p3: inverse(proj * rotation-only view);
// p4.y: brightness; p4.z > 0.5: write display-ready (tonemapped) color;
// p5.xyz: the sun's direction of travel.
#include "post_common.hlsli"

float3 ProceduralSky(float3 dir, float3 sun_dir)
{
    // Roughly Unity's default skybox: blue sky, grey horizon, dark ground.
    float3 sky_top = float3(0.18, 0.34, 0.62);
    float3 horizon = float3(0.62, 0.66, 0.70);
    float3 ground = float3(0.24, 0.22, 0.21);
    float y = dir.y;
    float3 color = y > 0.0 ? lerp(horizon, sky_top, pow(saturate(y), 0.45))
                           : lerp(horizon, ground, pow(saturate(-y * 4.0), 0.6));
    float sun = saturate(dot(dir, sun_dir));
    color += float3(1.0, 0.95, 0.85) * (pow(sun, 900.0) * 6.0 + pow(sun, 12.0) * 0.12);
    return color;
}

float4 Main(FullscreenOutput input) : SV_Target0
{
    float4x4 inverse_matrix = float4x4(params.p0, params.p1, params.p2, params.p3);
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 world = mul(transpose(inverse_matrix), float4(ndc, 1.0, 1.0));
    float3 dir = normalize(world.xyz / world.w);
    float3 color;
    if (params.p4.x > 0.5)
    {
        const float PI = 3.14159265358979323846;
        float u = atan2(dir.x, -dir.z) / (2.0 * PI) + 0.5;
        float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
        // The panorama is stored as sRGB bytes.
        color = input0.SampleLevel(linear_clamp, float2(u, v), 0).rgb;
        if (params.p4.z < 0.5) color = SrgbToLinear(color);
    }
    else
    {
        color = ProceduralSky(dir, -normalize(params.p5.xyz)) * params.p4.y;
        if (params.p4.z > 0.5)
        {
            color = LinearToSrgb(saturate((color * (2.51 * color + 0.03)) /
                                          (color * (2.43 * color + 0.59) + 0.14)));
        }
    }
    return float4(color, 1.0);
}
