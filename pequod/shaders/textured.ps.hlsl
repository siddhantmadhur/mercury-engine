Texture2D<float4> diffuse_texture : register(t0);
SamplerState texture_sampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target0;
};

PSOutput Main(PSInput input)
{
    PSOutput output = (PSOutput) 0;
    output.color = diffuse_texture.Sample(texture_sampler, input.uv) * input.color;
    return output;
}
