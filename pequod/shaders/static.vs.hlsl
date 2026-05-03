// Static vertex shader: positions are pre-baked into world space and atlas
// UVs ride per-vertex, so no per-object cbuffer is needed.
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
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
    output.position = mul(mWorldViewProj, float4(input.position, 1.0));

    // Snap to nearest pixel
    float2 halfRes = float2(mResolution.x * 0.5, mResolution.y * 0.5);
    output.position.xy = round(output.position.xy / output.position.w * halfRes) / halfRes * output.position.w;

    output.color = float4(input.color, 1.0);
    output.uv = input.atlas_uv.xy + input.uv * (input.atlas_uv.zw - input.atlas_uv.xy);
    return output;
}