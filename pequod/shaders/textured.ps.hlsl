// Cook-Torrance GGX PBR pixel shader.
// Sun is the only light at the moment; structure leaves room for more.

Texture2D<float4> diffuse_texture : register(t0);
Texture2D<float>  shadow_map      : register(t1);
SamplerState texture_sampler            : register(s0);
SamplerComparisonState shadow_sampler   : register(s1);

cbuffer PS_LIGHT_BUFFER : register(b0)
{
    float3 sun_direction;
    float  sun_intensity;
    float3 sun_color;
    float  _pad0;
    float3 ambient;
    float  _pad1;
    float3 camera_world_pos;
    float  _pad2;
    matrix mLightViewProj;
    float4 shadow_params; // x=texel_size y=bias z=enabled w=reserved
};

cbuffer PS_MATERIAL_BUFFER : register(b1)
{
    float3 mat_albedo;
    float  mat_metallic;
    float  mat_roughness;
    float  mat_ao;
    float2 _pad_mat;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float4 light_space_pos : TEXCOORD3;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

static const float PI = 3.14159265359f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float SampleShadow(float4 light_space_pos)
{
    // Reconstruct NDC then map to texture-space UV.
    float3 proj = light_space_pos.xyz / light_space_pos.w;
    float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0; // outside light frustum -> treat as lit
    float current = proj.z - shadow_params.y; // bias
    // 3x3 PCF
    float texel = shadow_params.x;
    float sum = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float2 off = float2(x, y) * texel;
            sum += shadow_map.SampleCmpLevelZero(shadow_sampler, uv + off, current);
        }
    }
    return sum / 9.0;
}

PSOutput Main(PSInput input)
{
    PSOutput output = (PSOutput) 0;

    float4 tex = diffuse_texture.Sample(texture_sampler, input.uv);
    float3 base_color = tex.rgb * input.color.rgb * mat_albedo;
    float alpha = tex.a * input.color.a;

    float3 N = normalize(input.world_normal);
    float3 V = normalize(camera_world_pos - input.world_pos);
    float3 L = normalize(-sun_direction);
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), base_color, mat_metallic);
    float  D  = DistributionGGX(N, H, mat_roughness);
    float  G  = GeometrySmith(N, V, L, mat_roughness);
    float3 F  = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = D * G * F;
    float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 1e-5;
    float3 specular = numerator / denom;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - mat_metallic);

    float3 radiance = sun_color * sun_intensity;

    float shadow = 1.0;
    if (shadow_params.z > 0.5)
    {
        shadow = SampleShadow(input.light_space_pos);
    }

    float3 Lo = (kD * base_color / PI + specular) * radiance * NdotL * shadow;
    float3 ambient_term = ambient * base_color * mat_ao;

    float3 color = ambient_term + Lo;
    // Reinhard-ish tonemap to keep highlights from blowing out the LDR target.
    color = color / (color + 1.0);
    // Gamma to sRGB.
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, alpha);
    return output;
}
