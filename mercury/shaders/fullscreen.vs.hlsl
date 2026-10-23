// One triangle covering the screen; uv (0,0) is the top-left corner.
#include "post_common.hlsli"

FullscreenOutput Main(uint id : SV_VertexID)
{
    FullscreenOutput output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.uv = uv;
    // The viewport flips Y (negative height), so NDC +Y is the top.
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}
