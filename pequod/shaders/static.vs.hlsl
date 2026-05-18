// Static vertex shader: positions are pre-baked into world space and atlas
// UVs ride per-vertex, so no per-object cbuffer is needed.
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

cbuffer VS_LIGHT_BUFFER : register(b2)
{
    matrix mLightViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 atlas_uv : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float4 light_space_pos : TEXCOORD3;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    float3 world = input.position;
    output.position = mul(mWorldViewProj, float4(world, 1.0));
    output.world_pos = world;
    output.world_normal = normalize(input.normal);
    output.light_space_pos = mul(mLightViewProj, float4(world, 1.0));

    output.color = input.color;
    output.uv = input.atlas_uv.xy + input.uv * (input.atlas_uv.zw - input.atlas_uv.xy);
    return output;
}
