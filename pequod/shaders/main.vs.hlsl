cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.position = mul(float4(input.position, 1.0), mWorldViewProj);
    output.color = input.color;
    return output;
}