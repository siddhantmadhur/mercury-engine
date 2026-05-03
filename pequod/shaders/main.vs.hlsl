// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

// Handles information on a per-model basis
cbuffer VS_MODEL_BUFFER : register(b1)
{
    float3 scale;
    float opacity;
    matrix model;
    float4 atlas_uv;
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

    // Snap to nearest pixel
    float2 halfRes = float2(mResolution.x * 0.5, mResolution.y * 0.5);
    output.position.xy = round(output.position.xy / output.position.w * halfRes) / halfRes * output.position.w;

    output.color = float4(input.color, opacity);
    output.uv = atlas_uv.xy + input.uv * (atlas_uv.zw - atlas_uv.xy);
    return output;
}