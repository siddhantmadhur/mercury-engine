// Final image: HDR color (input0) with SSAO (input1, where the normal
// target input2 allows it), bloom (input3), the outline (mask in input4),
// tonemapping, vignette and sRGB encoding.
//   p0: x AO intensity (0: off), y bloom intensity, z tonemap (0 none,
//       1 neutral, 2 ACES, 3 already display-ready), w exposure
//   p1: x vignette intensity, y vignette smoothness, zw vignette center
//   p2: outline color; p3: x outline width (px, 0: off)
//   p4: xy texel size of the output, z > 0.5: the target encodes sRGB
#include "post_common.hlsli"

float3 NeutralCurve(float3 x, float a, float b, float c, float d, float e, float f)
{
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}

// Unity's Neutral tonemapper (John Hable's curve, white point 5.3).
float3 NeutralTonemap(float3 x)
{
    const float a = 0.2, b = 0.29, c = 0.24, d = 0.272, e = 0.02, f = 0.3;
    const float white_level = 5.3;
    float3 white_scale = 1.0 / NeutralCurve(white_level.xxx, a, b, c, d, e, f);
    x = NeutralCurve(x * white_scale, a, b, c, d, e, f);
    return x * white_scale;
}

float3 AcesTonemap(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 Main(FullscreenOutput input) : SV_Target0
{
    float3 color = input0.SampleLevel(point_clamp, input.uv, 0).rgb;
    const int tonemap = (int) params.p0.z;

    if (params.p0.x > 0.0)
    {
        float allowed = input2.SampleLevel(point_clamp, input.uv, 0).a;
        float ao = input1.SampleLevel(linear_clamp, input.uv, 0).r;
        color *= lerp(1.0, lerp(1.0, ao, params.p0.x), allowed);
    }
    if (params.p0.y > 0.0)
    {
        color += input3.SampleLevel(linear_clamp, input.uv, 0).rgb * params.p0.y;
    }
    if (tonemap != 3)
    {
        color *= params.p0.w;
        if (tonemap == 1) color = NeutralTonemap(color);
        if (tonemap == 2) color = AcesTonemap(color);
    }
    if (params.p1.x > 0.0)
    {
        float2 d = abs(input.uv - params.p1.zw) * params.p1.x;
        d.x *= params.p4.y / params.p4.x;  // aspect-correct, like URP
        float factor = pow(saturate(1.0 - dot(d, d)), params.p1.y * 5.0 + 0.001);
        color *= factor;
    }
    if (tonemap != 3 && params.p4.z < 0.5) color = LinearToSrgb(color);

    if (params.p3.x > 0.0)
    {
        float center = input4.SampleLevel(point_clamp, input.uv, 0).r;
        if (center < 0.5)
        {
            float width = params.p3.x;
            float found = 0.0;
            const int kSteps = 16;
            for (int i = 0; i < kSteps; ++i)
            {
                float angle = i * (6.2831853 / kSteps);
                for (float r = 1.0; r <= width; r += 1.0)
                {
                    float2 uv = input.uv + float2(cos(angle), sin(angle)) * r * params.p4.xy;
                    found = max(found, input4.SampleLevel(point_clamp, uv, 0).r);
                }
            }
            color = lerp(color, params.p2.rgb, found * params.p2.a);
        }
    }
    return float4(saturate(color), 1.0);
}
