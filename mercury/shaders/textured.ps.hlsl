// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera and light uniform buffer (vertex + fragment)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
    float4 mLightDirection;  // xyz: the way the light travels
    float4 mLightColor;      // rgb: color * intensity
    float4 mAmbientColor;
    float4 mCameraPosition;
};

Texture2D<float4> diffuse_texture : register(t0);
SamplerState texture_sampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float3 world_position : POSITION1;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

PSOutput Main(PSInput input)
{
    PSOutput output = (PSOutput) 0;
    float4 base = diffuse_texture.Sample(texture_sampler, input.uv) * input.color;

    // Meshes without normals (sprites, 2D shapes) are drawn unlit.
    float normal_length = length(input.normal);
    if (normal_length < 0.001)
    {
        output.color = base;
        return output;
    }

    float3 n = input.normal / normal_length;
    float3 to_light = -normalize(mLightDirection.xyz);
    float diffuse = saturate(dot(n, to_light));
    float3 to_camera = normalize(mCameraPosition.xyz - input.world_position);
    float3 halfway = normalize(to_light + to_camera);
    float specular = pow(saturate(dot(n, halfway)), 32.0) * 0.25 * step(0.0, diffuse);

    float3 lit = base.rgb * (mAmbientColor.rgb + mLightColor.rgb * diffuse) +
                 mLightColor.rgb * specular;
    output.color = float4(lit, base.a);
    return output;
}
