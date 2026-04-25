// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
};

// Handles information on a per-model basis
cbuffer VS_MODEL_BUFFER : register(b1)
{
    float3 scale;
    float opacity;
    matrix model;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
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
    output.color = float4(input.color, opacity);
    output.uv = input.uv;
    return output;
}