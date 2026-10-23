// FXAA 3.11 (console quality) over the composited image (input0).
// p0.xy: texel size.
#include "post_common.hlsli"

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 Main(FullscreenOutput input) : SV_Target0
{
    float2 t = params.p0.xy;
    float2 uv = input.uv;
    float3 rgb_nw = input0.SampleLevel(linear_clamp, uv + float2(-1, -1) * t, 0).rgb;
    float3 rgb_ne = input0.SampleLevel(linear_clamp, uv + float2(1, -1) * t, 0).rgb;
    float3 rgb_sw = input0.SampleLevel(linear_clamp, uv + float2(-1, 1) * t, 0).rgb;
    float3 rgb_se = input0.SampleLevel(linear_clamp, uv + float2(1, 1) * t, 0).rgb;
    float3 rgb_m = input0.SampleLevel(linear_clamp, uv, 0).rgb;
    float luma_nw = Luma(rgb_nw), luma_ne = Luma(rgb_ne);
    float luma_sw = Luma(rgb_sw), luma_se = Luma(rgb_se), luma_m = Luma(rgb_m);
    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125)) return float4(rgb_m, 1.0);

    float2 dir = float2(-((luma_nw + luma_ne) - (luma_sw + luma_se)),
                        ((luma_nw + luma_sw) - (luma_ne + luma_se)));
    float reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * 0.03125, 1.0 / 128.0);
    float rcp_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp_min, -8.0, 8.0) * t;
    float3 rgb_a = 0.5 * (input0.SampleLevel(linear_clamp, uv + dir * (1.0 / 3.0 - 0.5), 0).rgb +
                          input0.SampleLevel(linear_clamp, uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);
    float3 rgb_b = rgb_a * 0.5 + 0.25 * (input0.SampleLevel(linear_clamp, uv + dir * -0.5, 0).rgb +
                                         input0.SampleLevel(linear_clamp, uv + dir * 0.5, 0).rgb);
    float luma_b = Luma(rgb_b);
    return float4((luma_b < luma_min || luma_b > luma_max) ? rgb_a : rgb_b, 1.0);
}
