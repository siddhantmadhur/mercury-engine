// Transmissive shadow casters (glass) into a transmittance map, cleared to
// white: rgb is multiplied by what each surface lets through (blend), and
// alpha keeps the nearest one's depth (blend op min). Sun maps take the
// rasterized depth; shadow cubes (shadow_params.w = 1) the distance to the
// cube's center over its far distance (light_info.z), as shadow_cube.ps.hlsl.
#include "model_common.hlsli"

float4 Main(ModelVSOutput input, float4 position : SV_Position) : SV_Target0
{
    DrawData data = draws[input.draw];
    float depth = position.z;
    if (shadow_params.w > 0.5)
    {
        float3 offset = input.world_position - camera_pos.xyz;
        if (light_dir.w > 0.5 && dot(offset, light_dir.xyz) < 0.0) discard;
        depth = saturate(length(offset) / light_info.z);
    }
    float transmission = saturate(data.transmission.w) * (1.0 - saturate(data.material.x));
    float ior = max(data.volume.x, 1.0);
    float f0 = (ior - 1.0) / (ior + 1.0);
    // Light crosses a closed object's surface twice, so each takes half the
    // tint (its square root), and loses the reflection both times.
    float3 through = sqrt(saturate(data.transmission.rgb)) * (1.0 - f0 * f0) * transmission;
    return float4(through, depth);
}
