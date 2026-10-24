// Model renderer pixel shader: Cook-Torrance GGX for the directional light
// (PCF shadows from the camera's map, and a scene-wide map past it), up to kMaxPointLights point lights and kMaxAreaLights
// rectangular area lights (soft cube-map shadows), and indirect light from
// the light probe volume, or hemispheric ambient outside one. Normal and
// emission maps are optional.
//
// In a probe capture pass (light_info.y) it writes the radiance a probe sees,
// with alpha 0 on back faces so probes inside walls can be found. Writes linear HDR color and a
// view-space normal for SSAO (alpha 0 in overlay passes: no AO there).
#include "model_common.hlsli"
#include "lighting.hlsli"

[[vk::binding(3, 0)]] Texture2D<float> shadow_map;
[[vk::binding(4, 0)]] SamplerComparisonState shadow_sampler;
[[vk::binding(5, 0)]] SamplerState material_sampler;
[[vk::binding(6, 0)]] SamplerState clamp_sampler;
[[vk::binding(9, 0)]] Texture2D<float> far_shadow_map;
[[vk::binding(0, 1)]] Texture2D textures[];

struct PSOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

static const float PI = 3.14159265358979323846;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
}

float3 FresnelSchlick(float cos_theta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cos_theta), 5.0);
}

float3 FresnelSchlickRoughness(float cos_theta, float3 F0, float roughness)
{
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) * pow(saturate(1.0 - cos_theta), 5.0);
}

// Diffuse + specular of one light arriving from direction L (unit, toward
// the light) with `radiance` (linear color * intensity * attenuation).
float3 DirectLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo,
                   float3 F0, float metallic, float roughness)
{
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = saturate(dot(N, H));
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    // Unity's lights are not divided by PI; neither is the diffuse here.
    return (kD * albedo + specular * PI) * radiance * NdotL;
}

// Unity's URP point light falloff: inverse square, faded to 0 at `range`.
float PointAttenuation(float distance_sq, float range)
{
    float ratio = distance_sq / max(range * range, 1e-4);
    float fade = saturate(1.0 - ratio * ratio);
    return fade * fade / max(distance_sq, 1e-4);
}

static const float2 kPoisson[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

// Where world_pos (pushed off the surface by `normal_bias`) falls in a sun
// shadow map: uv, depth, and how close to the map's edge (1 at it).
float3 ShadowCoord(float4x4 view_proj, float3 world_pos, float3 normal, float normal_bias,
                   out float edge)
{
    float4 clip = mul(view_proj, float4(world_pos + normal * normal_bias, 1.0));
    float3 p = clip.xyz / clip.w;
    edge = max(max(abs(p.x), abs(p.y)), abs(p.z * 2.0 - 1.0));
    return float3(p.x * 0.5 + 0.5, -p.y * 0.5 + 0.5, p.z);
}

float FilterShadow(Texture2D<float> map, float3 coord, float resolution, float bias)
{
    float texel = 1.0 / resolution;
    float lit = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        lit += map.SampleCmpLevelZero(shadow_sampler, coord.xy + kPoisson[i] * texel * 1.5,
                                      coord.z - bias);
    }
    return lit / 16.0;
}

// The sun's shadow: the camera's detailed map near the camera, the
// scene-wide map past it, blended over the last tenth of the near map.
float CalculateShadow(float3 world_pos, float3 normal, float3 to_light)
{
    bool far_only = far_shadow_params.w > 0.5;
    if (shadow_params.x <= 0.0 && !far_only) return 1.0;
    float slope = 1.0 - saturate(dot(normal, to_light)) * 0.7;

    float near_lit = 1.0;
    float far_weight = 1.0;
    if (!far_only)
    {
        float edge;
        float3 coord = ShadowCoord(light_view_proj, world_pos, normal,
                                   max(shadow_params.y, 0.01) * slope, edge);
        if (edge < 0.998)
        {
            near_lit = FilterShadow(shadow_map, coord, shadow_params.x, shadow_params.z);
            far_weight = saturate((edge - 0.9) / 0.098);
        }
    }
    if (far_weight <= 0.0) return near_lit;
    if (far_shadow_params.x <= 0.0) return lerp(near_lit, 1.0, far_weight);
    float far_edge;
    float3 far_coord = ShadowCoord(far_light_view_proj, world_pos, normal,
                                   far_shadow_params.y * slope, far_edge);
    float far_lit = far_edge < 0.999
                        ? FilterShadow(far_shadow_map, far_coord, far_shadow_params.x, 0.0)
                        : 1.0;
    return lerp(near_lit, far_lit, far_weight);
}

float3 AcesTonemap(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

PSOutput Main(ModelVSOutput input, bool front_face : SV_IsFrontFace)
{
    DrawData data = draws[input.draw];
    const uint draw_flags = data.info.y;

    float4 base = input.color;
    const uint texture_index = data.info.x;
    float4 texel = (draw_flags & kFlagClampTexture) != 0
        ? textures[NonUniformResourceIndex(texture_index)].Sample(clamp_sampler, input.uv)
        : textures[NonUniformResourceIndex(texture_index)].Sample(material_sampler, input.uv);

    if ((draw_flags & kFlagSdfText) != 0)
    {
        // Signed distance in alpha: 0.5 is the glyph edge.
        float distance = texel.a;
        float width = max(fwidth(distance) * 0.75, 1e-4);
        base.a *= smoothstep(0.5 - width, 0.5 + width, distance);
        if (base.a < 0.004) discard;
    }
    else
    {
        base *= texel;
    }

    PSOutput output = (PSOutput) 0;
    float3 N = normalize(input.normal);
    if (!front_face) N = -N;
    const float3 surface_normal = N;  // before the normal map, for shadow bias
    if (data.maps.x != 0 && dot(input.tangent.xyz, input.tangent.xyz) > 1e-12)
    {
        // Tangent-space normal, green toward the top of the image.
        float3 T = normalize(input.tangent.xyz - N * dot(N, input.tangent.xyz));
        float3 B = cross(N, T) * (input.tangent.w < 0.0 ? -1.0 : 1.0);
        float3 n = textures[NonUniformResourceIndex(data.maps.x)]
                       .Sample(material_sampler, input.uv).xyz * 2.0 - 1.0;
        n.xy *= data.material.z;
        N = normalize(T * n.x + B * n.y + N * max(n.z, 1e-3));
    }
    const bool capture = light_info.y > 0.5;
    float3 emission = data.emission.rgb * (capture ? light_info.w : 1.0);
    if (data.maps.y != 0)
    {
        emission *= textures[NonUniformResourceIndex(data.maps.y)]
                        .Sample(material_sampler, input.uv).rgb;
    }
    float3 view_normal = normalize(mul((float3x3) view, N));
    // Alpha: the share of the pixel's light that is indirect, which is all
    // SSAO darkens (0: none, as on overlays, transparent and unlit surfaces).
    output.normal = float4(view_normal * 0.5 + 0.5, 0.0);

    float3 color;
    if ((draw_flags & kFlagUnlit) != 0 || length(input.normal) < 1e-4)
    {
        color = base.rgb + emission;
    }
    else
    {
        float3 V = normalize(camera_pos.xyz - input.world_position);
        float3 L = -normalize(light_dir.xyz);
        float metallic = saturate(data.material.x);
        float roughness = clamp(1.0 - data.material.y, 0.045, 1.0);
        float3 albedo = base.rgb;
        float3 F0 = lerp(0.04.xxx, albedo, metallic);
        float NdotV = max(dot(N, V), 1e-4);

        float shadow = CalculateShadow(input.world_position, surface_normal, L);
        float3 direct = DirectLight(N, V, L, light_color.rgb * shadow, albedo, F0, metallic,
                                    roughness);
        const uint point_count = min((uint) flags.w, kMaxPointLights);
        for (uint i = 0; i < point_count; ++i)
        {
            float4 light = point_lights[2 * i];
            float3 to_light = light.xyz - input.world_position;
            float distance_sq = dot(to_light, to_light);
            if (distance_sq >= light.w * light.w) continue;
            float4 color = point_lights[2 * i + 1];
            float3 radiance = color.rgb * PointAttenuation(distance_sq, light.w);
            if (color.w >= 0.0)
            {
                float4 shadow_info = point_shadows[i];
                radiance *= CubeShadow((uint) color.w, input.world_position, surface_normal,
                                       shadow_info.x, shadow_info.y, shadow_info.z > 0.5,
                                       input.position.xy);
            }
            direct += DirectLight(N, V, to_light * rsqrt(max(distance_sq, 1e-8)), radiance,
                                  albedo, F0, metallic, roughness);
        }

        const uint area_count = min((uint) light_info.x, kMaxAreaLights);
        for (uint a = 0; a < area_count; ++a)
        {
            float4 a0 = area_lights[4 * a];
            float4 a1 = area_lights[4 * a + 1];
            float4 a2 = area_lights[4 * a + 2];
            float4 a3 = area_lights[4 * a + 3];
            float3 center = a0.xyz;
            float3 right = a1.xyz;
            float3 up = a2.xyz;
            float3 light_normal = normalize(cross(right, up));
            float3 from_light = input.world_position - center;
            float side = dot(from_light, light_normal);
            if (side <= 0.0)
            {
                if (a1.w < 0.5) continue;  // behind a single-sided light
                light_normal = -light_normal;
                side = -side;
            }
            float distance_sq = dot(from_light, from_light);
            if (distance_sq >= a0.w * a0.w) continue;
            float3 radiance = a3.rgb * RangeFade(distance_sq, a0.w);
            if (a2.w >= 0.0)
            {
                // The rectangle's half-diagonal stands for its size.
                radiance *= CubeShadow((uint) a2.w, input.world_position, surface_normal,
                                       length(right + up), area_shadow_bias[a].x, a3.w > 0.5,
                                       input.position.xy);
            }
            float3 corners[4] = {center - right - up, center + right - up,
                                 center + right + up, center - right + up};
            // Diffuse: exact for a Lambertian rectangle.
            float form_factor = PolygonFormFactor(input.world_position, N, corners);
            float3 F_view = FresnelSchlick(NdotV, F0);
            direct += (1.0 - F_view) * (1.0 - metallic) * albedo * radiance * form_factor;

            // Specular: the point of the rectangle nearest the mirror direction,
            // with the lobe widened by the light's size (Karis 2013).
            float3 R = reflect(-V, N);
            float denom = dot(R, -light_normal);
            float3 target = center;
            if (denom > 1e-4)
            {
                float3 hit = input.world_position + R * (side / denom) - center;
                float hx = clamp(dot(hit, right) / dot(right, right), -1.0, 1.0);
                float hy = clamp(dot(hit, up) / dot(up, up), -1.0, 1.0);
                target = center + right * hx + up * hy;
            }
            float3 to_target = target - input.world_position;
            float target_distance = length(to_target);
            float3 L_area = to_target / max(target_distance, 1e-5);
            float area = 4.0 * length(right) * length(up);
            float solid_angle = area * saturate(dot(-L_area, light_normal)) /
                                max(target_distance * target_distance, 1e-4);
            float alpha = roughness * roughness;
            float wide = saturate(alpha + 0.5 * sqrt(area) / max(target_distance, 1e-3));
            float3 H = normalize(V + L_area);
            float NdotL_area = saturate(dot(N, L_area));
            float D = DistributionGGX(saturate(dot(N, H)), sqrt(wide));
            float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL_area, roughness);
            float3 F_spec = FresnelSchlick(saturate(dot(H, V)), F0);
            float energy = (alpha / wide) * (alpha / wide);
            direct += D * G * F_spec / max(4.0 * NdotV * NdotL_area, 1e-4) * NdotL_area *
                      radiance * solid_angle * energy;
        }

        float3 ambient_light;
        if (!SampleProbes(input.world_position, N, ambient_light))
        {
            float hemi = N.y * 0.5 + 0.5;
            ambient_light = lerp(ambient_ground.rgb, ambient_sky.rgb, hemi);
        }
        float3 F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 ambient = (1.0 - F_ambient) * (1.0 - metallic) * albedo * ambient_light
                       + F_ambient * ambient_light * (1.0 - roughness) * 0.5;
        color = direct + ambient + emission;
        if (flags.y < 0.5 && (draw_flags & kFlagTransparent) == 0)
        {
            const float3 kLuma = float3(0.2126, 0.7152, 0.0722);
            float share = dot(ambient, kLuma) / max(dot(color, kLuma), 1e-6);
            output.normal.a = clamp(share, 1.0 / 255.0, 1.0);
        }
    }

    if (flags.x > 0.5)
    {
        color = LinearToSrgb(AcesTonemap(color));
    }
    output.color = float4(color, base.a);
    if (capture) output.color.a = front_face ? 1.0 : 0.0;
    return output;
}
