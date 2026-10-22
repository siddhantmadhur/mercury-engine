// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera and light uniform buffer (vertex + fragment)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   t1 -> binding 11  shadow map depth image        (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
//   s1 -> binding 21  shadow sampler                (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    matrix mLightViewProj;
    float4 mLightDirection;  // xyz: the way the light travels
    float4 mLightColor;      // rgb: color * intensity
    float4 mAmbientColor;    // rgb: ambient color
    float4 mCameraPosition;  // xyz: camera world position
    float2 mResolution;      // screen resolution
    float2 mShadowParams;    // x: shadow map resolution, y: normal bias
};

Texture2D<float4> diffuse_texture : register(t0);
Texture2D<float>  shadow_map      : register(t1);

SamplerState           texture_sampler : register(s0);
SamplerComparisonState shadow_sampler  : register(s1);

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float3 world_position : POSITION1;
    [[vk::location(4)]] float2 material_params : TEXCOORD1; // x: roughness, y: metallic
    [[vk::location(5)]] nointerpolation float4 atlas_rect : TEXCOORD2;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

static const float PI = 3.14159265358979323846;

// GGX / Trowbridge-Reitz Normal Distribution Function
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

// Smith-GGX Schlick Geometry masking-shadowing function
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 16-tap Poisson-disk filtered soft shadow calculation
float CalculateShadow(float3 world_pos, float3 normal, float3 to_light)
{
    if (mShadowParams.x <= 0.0) return 1.0;

    float NdotL = max(dot(normal, to_light), 0.0);
    if (NdotL <= 0.001) return 0.0;

    // Normal offset bias to eliminate shadow acne on inclined surfaces
    float normal_bias = max(mShadowParams.y, 0.02) * (1.0 - NdotL * 0.7);
    float3 biased_pos = world_pos + normal * normal_bias;

    float4 light_clip = mul(mLightViewProj, float4(biased_pos, 1.0));
    float3 proj = light_clip.xyz / light_clip.w;

    // Convert from light NDC [-1, 1] to UV [0, 1]
    // Viewport has negative height, so -proj.y corresponds to texture V coordinate.
    float2 shadow_uv = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    // Outside the shadow frustum -> fully lit
    if (shadow_uv.x < 0.001 || shadow_uv.x > 0.999 ||
        shadow_uv.y < 0.001 || shadow_uv.y > 0.999 ||
        proj.z < 0.0 || proj.z > 1.0)
    {
        return 1.0;
    }

    float current_depth = proj.z;
    float texel = 1.0 / max(mShadowParams.x, 1024.0);

    // 16-tap Poisson disc for smooth, filmic shadow penumbra
    static const float2 poisson_disk[16] = {
        float2(-0.94201624, -0.39906216),
        float2( 0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870),
        float2( 0.34495938,  0.29387760),
        float2(-0.91588581,  0.45771432),
        float2(-0.81544232, -0.87912464),
        float2(-0.38277543,  0.27676845),
        float2( 0.97484398,  0.75648379),
        float2( 0.44323325, -0.97511554),
        float2( 0.53742981, -0.47373420),
        float2(-0.26496911, -0.41893023),
        float2( 0.79197514,  0.19090188),
        float2(-0.24188840,  0.99706507),
        float2(-0.81409955,  0.91437590),
        float2( 0.19984126,  0.78641367),
        float2( 0.14383161, -0.14100790)
    };

    float shadow = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float2 offset = poisson_disk[i] * (texel * 1.5);
        shadow += shadow_map.SampleCmpLevelZero(shadow_sampler, shadow_uv + offset, current_depth);
    }
    return shadow / 16.0;
}

PSOutput Main(PSInput input)
{
    PSOutput output = (PSOutput) 0;
    // uv counts tiles; wrap it into the texture's rectangle in the atlas.
    float2 atlas_uv = input.atlas_rect.xy
                    + frac(input.uv) * (input.atlas_rect.zw - input.atlas_rect.xy);
    float4 base = diffuse_texture.Sample(texture_sampler, atlas_uv) * input.color;

    // Meshes without normals (sprites, 2D shapes, text) are drawn unlit.
    float normal_length = length(input.normal);
    if (normal_length < 0.001)
    {
        output.color = base;
        return output;
    }

    float3 N = input.normal / normal_length;
    float3 V = normalize(mCameraPosition.xyz - input.world_position);
    float3 to_light = -normalize(mLightDirection.xyz);
    float3 H = normalize(V + to_light);

    // PBR Material parameters
    float roughness = clamp(input.material_params.x, 0.04, 1.0);
    float metallic  = clamp(input.material_params.y, 0.0, 1.0);

    float3 albedo = base.rgb;
    // Dielectrics have base reflectance around 0.04; metals use their albedo as F0.
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Direct lighting: Cook-Torrance Microfacet BRDF
    float NdotL = max(dot(N, to_light), 0.0);
    float NdotV = max(dot(N, V), 1e-4);

    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, to_light, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 kS = F;
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);

    float3 diffuse = kD * (albedo / PI);
    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    float shadow = CalculateShadow(input.world_position, N, to_light);
    float3 direct = (diffuse + specular) * mLightColor.rgb * NdotL * shadow;

    // Multi-bounce / Hemispheric ambient lighting
    float hemi = dot(N, float3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    float3 sky_ambient = mAmbientColor.rgb;
    float3 ground_ambient = mAmbientColor.rgb * 0.35;
    float3 ambient_light = lerp(ground_ambient, sky_ambient, hemi);

    float3 F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kD_ambient = (1.0 - F_ambient) * (1.0 - metallic);
    float3 ambient_diffuse = kD_ambient * albedo * ambient_light;
    float3 ambient_specular = F_ambient * ambient_light * (1.0 - roughness * 0.5);
    float3 ambient = ambient_diffuse + ambient_specular;

    float3 lit = direct + ambient;

    // ACES Filmic Tone Mapping (standard in modern HDR engines)
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    float3 mapped = saturate((lit * (a * lit + b)) / (lit * (c * lit + d) + e));

    // Gamma correction: Linear -> sRGB
    float3 gamma_corrected = pow(mapped, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

    output.color = float4(gamma_corrected, base.a);
    return output;
}
