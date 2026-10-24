// Screen-space ambient occlusion from depth (input0) and view-space
// normals (input1), drawn at half resolution. p0: x = 1/proj[0][0], y = 1/proj[1][1],
// z = proj[2][2], w = proj[3][2]; p1: x radius, y intensity, z sample count;
// p2: proj[0][0], proj[1][1].
#include "post_common.hlsli"

float ViewDepth(float d)
{
    return -params.p0.w / (d + params.p0.z);  // view-space z (negative)
}

float3 ViewPosition(float2 uv, float d)
{
    float z = ViewDepth(d);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc.x * params.p0.x * -z, ndc.y * params.p0.y * -z, z);
}

float Hash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float4 Main(FullscreenOutput input) : SV_Target0
{
    float depth = input0.SampleLevel(point_clamp, input.uv, 0).r;
    float4 normal_data = input1.SampleLevel(point_clamp, input.uv, 0);
    // Alpha 0: nothing here takes ambient occlusion.
    if (depth >= 1.0 || normal_data.a <= 0.0) return 1.0.xxxx;
    float3 position = ViewPosition(input.uv, depth);
    float3 normal = normalize(normal_data.xyz * 2.0 - 1.0);

    float radius = params.p1.x;
    int samples = (int) params.p1.z;
    float angle = Hash(input.position.xy) * 6.2831853;
    float occlusion = 0.0;
    float3 tangent = normalize(abs(normal.y) < 0.99 ? cross(normal, float3(0, 1, 0))
                                                    : cross(normal, float3(1, 0, 0)));
    float3 bitangent = cross(normal, tangent);
    for (int i = 0; i < samples; ++i)
    {
        // Points on a spiral over the hemisphere around the normal.
        float t = (i + 0.5) / samples;
        float phi = angle + i * 2.39996323;
        float r = sqrt(t);
        float3 h = float3(cos(phi) * r, sin(phi) * r, sqrt(1.0 - t));
        float3 offset = (tangent * h.x + bitangent * h.y + normal * h.z) * radius * lerp(0.2, 1.0, t);
        float3 sample_position = position + offset;
        float2 sample_ndc = float2(sample_position.x * params.p2.x, sample_position.y * params.p2.y)
                          / -sample_position.z;
        float2 sample_uv = float2(sample_ndc.x * 0.5 + 0.5, 0.5 - sample_ndc.y * 0.5);
        if (any(sample_uv < 0.0) || any(sample_uv > 1.0)) continue;
        float scene_z = ViewDepth(input0.SampleLevel(point_clamp, sample_uv, 0).r);
        float range = saturate(radius / max(abs(position.z - scene_z), 1e-4));
        occlusion += (scene_z >= sample_position.z + 0.02 ? 1.0 : 0.0) * range;
    }
    // Squared: creases read clearly while open surfaces stay untouched.
    float ao = 1.0 - occlusion / max(samples, 1);
    ao *= ao;
    return float4(ao.xxx, 1.0);
}
