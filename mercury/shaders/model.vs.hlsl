// Model renderer vertex shader; see model_common.hlsli for the bindings.
#include "model_common.hlsli"

// Invariant (18): the depth prepass and the main pass, different pipelines
// over this shader, must place vertices identically.
ModelVSOutput Main(ModelVSInput input,
                   [[vk::ext_decorate(18)]] out precise float4 position : SV_Position)
{
    DrawData data = draws[input.draw];
    float4 local;
    float3 local_normal;
    float3 local_tangent;
    SkinVertex(input, data, local, local_normal, local_tangent);

    ModelVSOutput output = (ModelVSOutput) 0;
    float4 world_position = mul(data.world, local);
    position = mul(view_proj, world_position);
    output.world_position = world_position.xyz;
    output.normal = mul((float3x3) data.normal_matrix, local_normal);
    output.tangent = float4(mul((float3x3) data.world, local_tangent), input.tangent.w);
    output.uv = input.uv * data.uv.xy + data.uv.zw;
    output.color = input.color * data.color;
    output.draw = input.draw;
    return output;
}
