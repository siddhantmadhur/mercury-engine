// Cube-map shadows of point and area lights and the light probe volume, for
// model.ps.hlsl. Include after model_common.hlsli.
//
// Descriptor set 0 (continued from model_common.hlsli):
//   binding 7  Texture2DArray<float>  shadow cubes: 6 faces per slot, each
//                                     texel the distance to the nearest caster
//   binding 8  Texture2D<float4>      probe atlas: 9 texels per probe, the
//                                     L2 spherical harmonics of its radiance
//   binding 10 Texture2D<float2>      probe visibility: an octahedral map
//                                     per probe of the distance to the
//                                     nearest surface (mean, mean square)

#include "lighting_math.hlsli"

[[vk::binding(7, 0)]] Texture2DArray<float> shadow_cubes;
[[vk::binding(8, 0)]] Texture2D<float4> probe_atlas;
[[vk::binding(10, 0)]] Texture2D<float2> probe_visibility;

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

// One probe's light toward d from its radiance SH, each band (l = 0, 1, 2)
// scaled by `bands`: the cosine lobe's (kCosineBands) gives irradiance / pi,
// the radiance a white diffuse surface reflects; kSharpBands the sharpest
// radiance L2 holds without ringing (a Hann window: unwindowed, a mirror
// shows colored blotches that are in no direction).
static const float3 kCosineBands = float3(1.0, 2.0 / 3.0, 0.25);
static const float3 kSharpBands = float3(1.0, 0.75, 0.25);

float3 ProbeLight(uint probe, float3 d, float3 bands)
{
    float basis[9];
    ShBasis(d, basis);
    float3 e = probe_atlas.Load(int3(ProbeTexel(probe, 0), 0)).rgb * (bands.x * basis[0]);
    [unroll]
    for (uint i = 1; i < 9; ++i)
    {
        float band = i < 4 ? bands.y : bands.z;
        e += probe_atlas.Load(int3(ProbeTexel(probe, i), 0)).rgb * (band * basis[i]);
    }
    return max(e, 0.0);
}

// Mean and mean square of the distance from `probe` to the nearest surface
// along `d`, bilinear within the probe's map.
float2 ProbeDistance(uint probe, float3 d)
{
    const int size = (int) kProbeVisibilitySize;
    uint per_row = (uint) gi_counts.w;
    int2 corner = int2(probe % per_row, probe / per_row) * size;
    float2 uv = (OctEncode(d) * 0.5 + 0.5) * size - 0.5;
    int2 i = (int2) floor(uv);
    float2 f = uv - (float2) i;
    float2 m00 = probe_visibility.Load(int3(corner + clamp(i, 0, size - 1), 0));
    float2 m10 = probe_visibility.Load(int3(corner + clamp(i + int2(1, 0), 0, size - 1), 0));
    float2 m01 = probe_visibility.Load(int3(corner + clamp(i + int2(0, 1), 0, size - 1), 0));
    float2 m11 = probe_visibility.Load(int3(corner + clamp(i + int2(1, 1), 0, size - 1), 0));
    return lerp(lerp(m00, m10, f.x), lerp(m01, m11, f.x), f.y);
}

// How likely `probe` sees a point `distance` away along `d`: 1 in front of
// the surfaces it saw there, falling off behind them (Chebyshev, as DDGI).
float ProbeVisibility(uint probe, float3 d, float distance)
{
    float2 moments = ProbeDistance(probe, d);
    if (distance <= moments.x) return 1.0;
    float variance = abs(moments.y - moments.x * moments.x);
    float gap = distance - moments.x;
    float chebyshev = variance / (variance + gap * gap);
    return max(chebyshev * chebyshev * chebyshev, 0.0);
}

// The probes' indirect light at a surface, blended from the 8 around it:
// `light` the diffuse part toward the shading normal n (irradiance / pi),
// `reflected` the light arriving along the reflection r, blurred more the
// rougher the surface. Probes inside walls, ones behind the surface (its
// geometric normal) and ones a wall hides from it count less, so light does
// not leak through walls. `eye` is the camera position. Returns false outside
// the volume or when no probe is usable.
bool SampleProbes(float3 p, float3 surface_normal, float3 n, float3 eye, float3 r,
                  float roughness, out float3 light, out float3 reflected)
{
    light = 0.0;
    reflected = 0.0;
    float3 gloss_bands = lerp(kSharpBands, kCosineBands, saturate(roughness));
    if (gi_min.w < 0.5) return false;
    float3 extent = gi_max.xyz - gi_min.xyz;
    float3 spacing = extent / max(gi_counts.xyz - 1.0, 1.0);
    float min_spacing = min(spacing.x, min(spacing.y, spacing.z));
    // Off the surface and toward the camera (as DDGI): near a wall the point
    // then sits clearly in front of it, so probes beyond the wall stay hidden
    // even where the floor meets it. The camera sees p, so the way to it is
    // open space.
    float3 to_eye = eye - p;
    float eye_distance = length(to_eye);
    float3 q = p + surface_normal * min_spacing * 0.2 +
               to_eye / max(eye_distance, 1e-5) * min(min_spacing * 0.3, eye_distance * 0.5);
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
        uint probe = ProbeIndex(cell);
        float valid = probe_atlas.Load(int3(ProbeTexel(probe, 0), 0)).a;
        if (valid < 0.5) continue;
        float3 probe_position = ProbePosition(cell);
        float3 to_probe = normalize(probe_position - p + 1e-5);
        float facing = (dot(to_probe, surface_normal) + 1.0) * 0.5;
        float w = facing * facing + 0.05;
        // From the offset point, so the surface itself does not hide probes.
        float3 from_probe = q - probe_position;
        float reach = length(from_probe);
        w *= max(ProbeVisibility(probe, from_probe / max(reach, 1e-5), reach), 1e-4);
        // Crush small weights further, so a hidden probe barely counts.
        if (w < 0.2) w = w * w * w / 0.04;
        w *= tri.x * tri.y * tri.z;
        light += ProbeLight(probe, n, kCosineBands) * w;
        reflected += ProbeLight(probe, r, gloss_bands) * w;
        total += w;
    }
    if (total < 1e-9) return false;
    light = light / total * gi_max.w;
    reflected = reflected / total * gi_max.w;
    return true;
}
