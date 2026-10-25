// The scene behind transmissive surfaces: the main pass's color so far
// (input0) with the view distance of each pixel (from depth, input1) in
// alpha, for model.ps.hlsl to refract. p0..p3: inverse(projection).
#include "post_common.hlsli"

float4 Main(FullscreenOutput input) : SV_Target0
{
    float3 color = input0.SampleLevel(point_clamp, input.uv, 0).rgb;
    float depth = input1.SampleLevel(point_clamp, input.uv, 0).r;
    float4x4 inverse_proj = transpose(float4x4(params.p0, params.p1, params.p2, params.p3));
    float4 view = mul(inverse_proj, float4(0.0, 0.0, depth, 1.0));
    float distance = -view.z / view.w;
    return float4(color, depth >= 1.0 ? 65504.0 : distance);
}
