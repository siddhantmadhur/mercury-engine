// Cube-map shadows of point and area lights and the light probe volume, for
// model.ps.hlsl. Include after model_common.hlsli.
//
// Descriptor set 0 (continued from model_common.hlsli):
//   binding 7  Texture2DArray<float>  shadow cubes: 6 faces per slot, each
//                                     texel the distance to the nearest caster
//   binding 8  Texture2D<float4>      probe atlas: 9 texels per probe, the
//                                     L2 spherical harmonics of its radiance

#include "lighting_math.hlsli"

[[vk::binding(7, 0)]] Texture2DArray<float> shadow_cubes;
[[vk::binding(8, 0)]] Texture2D<float4> probe_atlas;

// ---------------------------------------------------------- cube shadows --

// Distance stored in slot `slot` toward direction d.
float ShadowCubeDistance(uint slot, float3 d)
{
    uint face = CubeFace(d);
    float2 uv = CubeFaceUV(d, face);
    float size = light_info.z;
    int2 texel = int2(clamp(uv * size, 0.0, size - 1.0));
    return shadow_cubes.Load(int4(texel, slot * 6 + face, 0));
}

static const uint kShadowSamples = 24;

// Sample i of n on a unit disk (Vogel's spiral), turned by `angle`.
float2 DiskSample(uint i, uint n, float angle)
{
    float r = sqrt((i + 0.5) / n);
    float theta = i * 2.39996323 + angle;
    return r * float2(cos(theta), sin(theta));
}

// Interleaved gradient noise (Jimenez 2014): a per-pixel value in [0, 1)
// whose pattern reads as fine grain rather than blotches.
float PixelNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// Fraction of a light of radius `radius` (world units, at the cube's center)
// that `world_pos` sees. Soft: percentage-closer soft shadows, the penumbra
// growing with the gap between receiver and blocker; otherwise a small PCF.
float CubeShadow(uint slot, float3 world_pos, float3 normal, float radius, float bias,
                 bool soft, float2 pixel)
{
    float4 cube = shadow_cubes_info[slot];
    float3 to_point = world_pos + normal * bias * 0.5 - cube.xyz;
    float receiver = length(to_point);
    if (receiver >= cube.w) return 1.0;
    float3 d = to_point / max(receiver, 1e-6);
    // A basis across the direction, for sampling a disk of directions.
    float3 t1 = normalize(abs(d.y) < 0.99 ? cross(d, float3(0, 1, 0)) : cross(d, float3(1, 0, 0)));
    float3 t2 = cross(d, t1);
    // The sample pattern turns from pixel to pixel, trading banding for grain.
    float angle = PixelNoise(pixel) * 6.2831853;
    float texel = 2.5 / light_info.z;  // a little over one texel, in radians

    float filter = texel;
    if (soft)
    {
        // Blocker search over the cone the light subtends from the receiver.
        float search = clamp(radius / receiver, texel, 0.5);
        float blockers = 0.0;
        float count = 0.0;
        for (uint i = 0; i < kShadowSamples; ++i)
        {
            float2 o = DiskSample(i, kShadowSamples, angle) * search;
            float stored = ShadowCubeDistance(slot, normalize(d + t1 * o.x + t2 * o.y));
            if (stored < receiver - bias)
            {
                blockers += stored;
                count += 1.0;
            }
        }
        if (count < 0.5) return 1.0;
        float blocker = blockers / count;
        float penumbra = radius * (receiver - blocker) / max(blocker, 1e-3);
        filter = clamp(penumbra / receiver, texel, 0.5);
    }
    float lit = 0.0;
    for (uint j = 0; j < kShadowSamples; ++j)
    {
        float2 o = DiskSample(j, kShadowSamples, angle) * filter;
        float stored = ShadowCubeDistance(slot, normalize(d + t1 * o.x + t2 * o.y));
        lit += stored >= receiver - bias ? 1.0 : 0.0;
    }
    return lit / kShadowSamples;
}

// ---------------------------------------------------------- light probes --

int2 ProbeTexel(uint probe, uint coefficient)
{
    uint per_row = (uint) gi_counts.w;
    return int2((probe % per_row) * kProbeCoefficients + coefficient, probe / per_row);
}

uint ProbeIndex(int3 cell)
{
    int3 counts = int3(gi_counts.xyz);
    return (uint) (cell.x + counts.x * (cell.y + counts.y * cell.z));
}

float3 ProbePosition(int3 cell)
{
    float3 counts = gi_counts.xyz;
    return lerp(gi_min.xyz, gi_max.xyz, (float3) cell / max(counts - 1.0, 1.0));
}

// Irradiance / pi (the radiance a white diffuse surface reflects) at n from
// one probe, from its radiance SH convolved with the cosine lobe.
float3 ProbeIrradiance(uint probe, float3 n)
{
    static const float kBand[9] = {
        1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25};
    float basis[9];
    ShBasis(n, basis);
    float3 e = 0.0;
    [unroll]
    for (uint i = 0; i < 9; ++i)
    {
        e += probe_atlas.Load(int3(ProbeTexel(probe, i), 0)).rgb * (kBand[i] * basis[i]);
    }
    return max(e, 0.0);
}

// The probes' indirect light at a surface (as ProbeIrradiance), blended from
// the 8 around it. Probes inside walls and ones behind the surface count
// less. Returns false outside the volume or when no probe is usable.
bool SampleProbes(float3 p, float3 n, out float3 light)
{
    light = 0.0;
    if (gi_min.w < 0.5) return false;
    float3 extent = gi_max.xyz - gi_min.xyz;
    float3 spacing = extent / max(gi_counts.xyz - 1.0, 1.0);
    // Off the surface a little, so the probes behind it matter less.
    float3 q = p + n * min(spacing.x, min(spacing.y, spacing.z)) * 0.3;
    float3 g = (q - gi_min.xyz) / spacing;
    float3 counts = gi_counts.xyz;
    if (any(g < -0.5) || any(g > counts - 0.5)) return false;
    g = clamp(g, 0.0, counts - 1.0);
    int3 base = min((int3) floor(g), (int3) counts - 2);
    float3 t = g - (float3) base;
    float total = 0.0;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner)
    {
        int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        int3 cell = base + offset;
        float3 tri = lerp(1.0 - t, t, (float3) offset);
        float w = tri.x * tri.y * tri.z;
        uint probe = ProbeIndex(cell);
        float valid = probe_atlas.Load(int3(ProbeTexel(probe, 0), 0)).a;
        float3 to_probe = normalize(ProbePosition(cell) - p + 1e-5);
        float facing = (dot(to_probe, n) + 1.0) * 0.5;
        w *= valid * (facing * facing + 0.05);
        if (w <= 1e-6) continue;
        light += ProbeIrradiance(probe, n) * w;
        total += w;
    }
    if (total < 1e-4) return false;
    light = light / total * gi_max.w;
    return true;
}
