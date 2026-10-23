// Bloom: 13-tap downsample of input0 (p0.xy: source texel size). The first
// pass (p0.z > 0.5) also applies the threshold p1.x with knee p1.y.
#include "post_common.hlsli"

float3 Prefilter(float3 c)
{
    float brightness = max(c.r, max(c.g, c.b));
    float threshold = params.p1.x;
    float knee = threshold * params.p1.y + 1e-5;
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
    return c * contribution;
}

float4 Main(FullscreenOutput input) : SV_Target0
{
    float2 t = params.p0.xy;
    float2 uv = input.uv;
    float3 a = input0.SampleLevel(linear_clamp, uv + t * float2(-2, -2), 0).rgb;
    float3 b = input0.SampleLevel(linear_clamp, uv + t * float2(0, -2), 0).rgb;
    float3 c = input0.SampleLevel(linear_clamp, uv + t * float2(2, -2), 0).rgb;
    float3 d = input0.SampleLevel(linear_clamp, uv + t * float2(-2, 0), 0).rgb;
    float3 e = input0.SampleLevel(linear_clamp, uv, 0).rgb;
    float3 f = input0.SampleLevel(linear_clamp, uv + t * float2(2, 0), 0).rgb;
    float3 g = input0.SampleLevel(linear_clamp, uv + t * float2(-2, 2), 0).rgb;
    float3 h = input0.SampleLevel(linear_clamp, uv + t * float2(0, 2), 0).rgb;
    float3 i = input0.SampleLevel(linear_clamp, uv + t * float2(2, 2), 0).rgb;
    float3 j = input0.SampleLevel(linear_clamp, uv + t * float2(-1, -1), 0).rgb;
    float3 k = input0.SampleLevel(linear_clamp, uv + t * float2(1, -1), 0).rgb;
    float3 l = input0.SampleLevel(linear_clamp, uv + t * float2(-1, 1), 0).rgb;
    float3 m = input0.SampleLevel(linear_clamp, uv + t * float2(1, 1), 0).rgb;
    float3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 +
                   (j + k + l + m) * 0.125;
    if (params.p0.z > 0.5) color = Prefilter(min(color, 65000.0.xxx));
    return float4(color, 1.0);
}
