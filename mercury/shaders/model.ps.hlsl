// Model renderer pixel shader: Cook-Torrance GGX for the directional light
// with PCF shadows, plus hemispheric ambient. Writes linear HDR color and a
// view-space normal for SSAO (alpha 0 in overlay passes: no AO there).
#include "model_common.hlsli"

[[vk::binding(3, 0)]] Texture2D<float> shadow_map;
[[vk::binding(4, 0)]] SamplerComparisonState shadow_sampler;
[[vk::binding(5, 0)]] SamplerState material_sampler;
[[vk::binding(6, 0)]] SamplerState clamp_sampler;
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

float CalculateShadow(float3 world_pos, float3 normal, float3 to_light)
{
    if (shadow_params.x <= 0.0) return 1.0;
    float NdotL = saturate(dot(normal, to_light));
    float normal_bias = max(shadow_params.y, 0.01) * (1.0 - NdotL * 0.7);
    float4 light_clip = mul(light_view_proj, float4(world_pos + normal * normal_bias, 1.0));
    float3 p = light_clip.xyz / light_clip.w;
    float2 shadow_uv = float2(p.x * 0.5 + 0.5, -p.y * 0.5 + 0.5);
    if (any(shadow_uv < 0.001) || any(shadow_uv > 0.999) || p.z < 0.0 || p.z > 1.0) return 1.0;

    static const float2 poisson[16] = {
        float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
        float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
        float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
        float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
        float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
        float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
        float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
    };
    float texel = 1.0 / shadow_params.x;
    float depth = p.z - shadow_params.z;
    float lit = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        lit += shadow_map.SampleCmpLevelZero(shadow_sampler, shadow_uv + poisson[i] * texel * 1.5, depth);
    }
    return lit / 16.0;
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
    float3 view_normal = normalize(mul((float3x3) view, N));
    output.normal = float4(view_normal * 0.5 + 0.5, (flags.y > 0.5) ? 0.0 : 1.0);
    if ((draw_flags & kFlagTransparent) != 0) output.normal.a = 0.0;

    float3 color;
    if ((draw_flags & kFlagUnlit) != 0 || length(input.normal) < 1e-4)
    {
        color = base.rgb + data.emission.rgb;
    }
    else
    {
        float3 V = normalize(camera_pos.xyz - input.world_position);
        float3 L = -normalize(light_dir.xyz);
        float3 H = normalize(V + L);
        float metallic = saturate(data.material.x);
        float roughness = clamp(1.0 - data.material.y, 0.045, 1.0);
        float3 albedo = base.rgb;
        float3 F0 = lerp(0.04.xxx, albedo, metallic);

        float NdotL = saturate(dot(N, L));
        float NdotV = max(dot(N, V), 1e-4);
        float NdotH = saturate(dot(N, H));
        float D = DistributionGGX(NdotH, roughness);
        float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
        float shadow = CalculateShadow(input.world_position, N, L);
        // Unity's lights are not divided by PI; neither is the diffuse here.
        float3 direct = (kD * albedo + specular * PI) * light_color.rgb * NdotL * shadow;

        float hemi = N.y * 0.5 + 0.5;
        float3 ambient_light = lerp(ambient_ground.rgb, ambient_sky.rgb, hemi);
        float3 F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 ambient = (1.0 - F_ambient) * (1.0 - metallic) * albedo * ambient_light
                       + F_ambient * ambient_light * (1.0 - roughness) * 0.5;
        color = direct + ambient + data.emission.rgb;
    }

    if (flags.x > 0.5)
    {
        color = LinearToSrgb(AcesTonemap(color));
    }
    output.color = float4(color, base.a);
    return output;
}
