// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float4 atlas_uv : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    // proj * view * model * pos
    float3 scaled_position = input.position * scale * 0.5;
    float4 world_pos = mul(model, float4(scaled_position, 1.0));
    output.position = mul(mWorldViewProj, world_pos);
    output.color = input.color;
    output.uv = atlas_uv.xy + input.uv * (atlas_uv.zw - atlas_uv.xy);
    return output;
}