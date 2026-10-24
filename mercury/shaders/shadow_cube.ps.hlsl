// Writes the distance from a point or area light's cube center (the pass's
// camera_pos) to the nearest surface, for lighting.hlsli's CubeShadow.
// A single-sided area light (light_dir.w = 1) cannot be blocked by what is
// behind it, such as the ceiling it hangs from: light_dir.xyz is the way it
// shines, and surfaces behind that plane are left out.
#include "model_common.hlsli"

float Main(ModelVSOutput input) : SV_Target0
{
    float3 offset = input.world_position - camera_pos.xyz;
    if (light_dir.w > 0.5 && dot(offset, light_dir.xyz) < 0.0) discard;
    return length(offset);
}
