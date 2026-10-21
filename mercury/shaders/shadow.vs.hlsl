// Vulkan resource mapping for shadow pass (descriptor set 0).
// b0 -> binding 0   shadow light view-proj uniform buffer

cbuffer VS_SHADOW_BUFFER : register(b0)
{
    matrix mLightViewProj;
};

struct VSInput
{
    // Per-vertex data (vertex buffer binding 0, layout matches Vertex)
    [[vk::location(0)]] float3 position : POSITION;

    // Per-instance data (vertex buffer binding 1, layout matches VsModelBuffer)
    [[vk::location(3)]] float3 inst_scale : INSTANCE_SCALE;
    [[vk::location(5)]] float4 inst_world0 : INSTANCE_WORLD0;
    [[vk::location(6)]] float4 inst_world1 : INSTANCE_WORLD1;
    [[vk::location(7)]] float4 inst_world2 : INSTANCE_WORLD2;
    [[vk::location(8)]] float4 inst_world3 : INSTANCE_WORLD3;
};

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    matrix world = matrix(input.inst_world0,
                          input.inst_world1,
                          input.inst_world2,
                          input.inst_world3);

    float3 scaled_position = input.position * input.inst_scale;
    float4 world_pos = mul(float4(scaled_position, 1.0), world);

    output.position = mul(mLightViewProj, world_pos);
    return output;
}
