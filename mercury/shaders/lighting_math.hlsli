// Lighting math without bindings, shared by model.ps.hlsl (through
// lighting.hlsli) and the probe passes.
#ifndef MERCURY_LIGHTING_MATH_HLSLI
#define MERCURY_LIGHTING_MATH_HLSLI

static const float LIGHTING_PI = 3.14159265358979323846;

// ------------------------------------------------------------ cube faces --
// Face f looks along kFaceForward[f] with kFaceUp[f] up (glm::lookAt), so its
// right is cross(forward, up). The renderer builds the same views.

static const float3 kFaceForward[6] = {
    float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0),
    float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 kFaceUp[6] = {
    float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1),
    float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

uint CubeFace(float3 d)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x >= 0.0 ? 0 : 1;
    if (a.y >= a.z) return d.y >= 0.0 ? 2 : 3;
    return d.z >= 0.0 ? 4 : 5;
}

// Texel coordinates (0..1, row 0 at the top) of direction d on face f.
float2 CubeFaceUV(float3 d, uint f)
{
    float3 forward = kFaceForward[f];
    float3 up = kFaceUp[f];
    float3 right = cross(forward, up);
    float z = dot(d, forward);
    float2 ndc = float2(dot(d, right), dot(d, up)) / max(z, 1e-6);
    return float2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
}

// Direction through texel uv of face f (the inverse of CubeFaceUV).
float3 CubeFaceDirection(float2 uv, uint f)
{
    float3 forward = kFaceForward[f];
    float3 up = kFaceUp[f];
    float3 right = cross(forward, up);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return normalize(forward + ndc.x * right + ndc.y * up);
}

// ----------------------------------------------------------- area lights --

// Irradiance from a uniform, Lambertian polygon of unit radiance, divided by
// pi (the form factor), for a surface at p facing n: Lambert's formula over
// the polygon clipped to the surface's upper hemisphere.
float PolygonFormFactor(float3 p, float3 n, float3 corners[4])
{
    // Clip against the plane through p with normal n (Sutherland-Hodgman).
    float3 clipped[5];
    uint count = 0;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float3 a = corners[i] - p;
        float3 b = corners[(i + 1) % 4] - p;
        float da = dot(a, n);
        float db = dot(b, n);
        if (da >= 0.0 && count < 5) clipped[count++] = a;
        if ((da >= 0.0) != (db >= 0.0) && count < 5)
        {
            clipped[count++] = lerp(a, b, da / (da - db));
        }
    }
    if (count < 3) return 0.0;
    float sum = 0.0;
    for (uint k = 0; k < count; ++k)
    {
        float3 a = normalize(clipped[k]);
        float3 b = normalize(clipped[(k + 1) % count]);
        float cos_theta = clamp(dot(a, b), -0.9999, 0.9999);
        float3 c = cross(a, b);
        float len = length(c);
        if (len < 1e-7) continue;
        sum += acos(cos_theta) * dot(c / len, n);
    }
    return abs(sum) / (2.0 * LIGHTING_PI);
}

// Smooth fade to 0 at `range`, so lights can be culled by distance.
float RangeFade(float distance_sq, float range)
{
    float ratio = distance_sq / max(range * range, 1e-4);
    float fade = saturate(1.0 - ratio * ratio);
    return fade * fade;
}

// ------------------------------------------------- spherical harmonics --

static const uint kProbeCoefficients = 9;

// Probe visibility: an octahedral map per probe of the distance to the
// nearest surface (mean and mean square), kProbeVisibilitySize^2 texels
// inside a one-texel border.
static const uint kProbeVisibilitySize = 8;
static const uint kProbeVisibilityTexels = kProbeVisibilitySize + 2;

// Probe light: octahedral maps of kProbeOctaSize^2 texels per probe inside
// a one-texel border (probe_octa.ps.hlsl). Must match kProbeOctaSize in
// vk_scene_renderer.h.
static const uint kProbeOctaSize = 6;
static const uint kProbeOctaTexels = kProbeOctaSize + 2;

// Band weights (l = 0, 1, 2) for evaluating a probe's radiance SH: the
// cosine lobe's (kCosineBands) give irradiance / pi, the radiance a white
// diffuse surface reflects; kSharpBands the sharpest radiance L2 holds
// without ringing (a Hann window: unwindowed, a mirror shows colored blotches
// that are in no direction).
static const float3 kCosineBands = float3(1.0, 2.0 / 3.0, 0.25);
static const float3 kSharpBands = float3(1.0, 0.75, 0.25);

// A probe capture's alpha: the signed distance to what the texel sees
// (negative on back faces), or more than kProbeSkyAlpha where it saw nothing.
static const float kProbeSkyAlpha = 60000.0;

float2 SignNotZero(float2 v)
{
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

// Unit direction <-> [-1, 1]^2 on the octahedron.
float2 OctEncode(float3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return d.z >= 0.0 ? d.xy : (1.0 - abs(d.yx)) * SignNotZero(d.xy);
}

// A point just past the octahedral square's edge, as the point inside it
// with the same direction: the map folds over at its edges.
float2 OctFold(float2 e)
{
    if (abs(e.x) > 1.0)
    {
        e.x = (e.x > 0.0 ? 2.0 : -2.0) - e.x;
        e.y = -e.y;
    }
    if (abs(e.y) > 1.0)
    {
        e.y = (e.y > 0.0 ? 2.0 : -2.0) - e.y;
        e.x = -e.x;
    }
    return e;
}

float3 OctDecode(float2 e)
{
    float3 d = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * SignNotZero(d.xy);
    return normalize(d);
}

// Real spherical harmonics up to band 2, in the usual order.
void ShBasis(float3 d, out float basis[9])
{
    basis[0] = 0.282095;
    basis[1] = 0.488603 * d.y;
    basis[2] = 0.488603 * d.z;
    basis[3] = 0.488603 * d.x;
    basis[4] = 1.092548 * d.x * d.y;
    basis[5] = 1.092548 * d.y * d.z;
    basis[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    basis[7] = 1.092548 * d.x * d.z;
    basis[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

#endif  // MERCURY_LIGHTING_MATH_HLSLI
