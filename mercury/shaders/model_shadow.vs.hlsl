// Depth-only vertex shader for the directional light's shadow map; the
// frame's view_proj is the light's.
#include "model_common.hlsli"

float4 Main(ModelVSInput input) : SV_Position
{
    DrawData data = draws[input.draw];
    float4 local;
    float3 local_normal;
    float3 local_tangent;
    SkinVertex(input, data, local, local_normal, local_tangent);
    return mul(view_proj, mul(data.world, local));
}
