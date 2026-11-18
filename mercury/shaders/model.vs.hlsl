// Basic model vertex shader; see model_common.hlsli for the bindings.
#include "model_common.hlsli"

ModelVSOutput Main(ModelVSInput input)
{
    DrawData data = draws[input.draw];
    float4 local = SkinPosition(input, data);

    ModelVSOutput output = (ModelVSOutput) 0;
    float4 world_position = mul(data.world, local);
    output.position = mul(view_proj, world_position);
    output.uv = input.uv * data.uv.xy + data.uv.zw;
    output.color = input.color * data.color;
    output.draw = input.draw;
    return output;
}
