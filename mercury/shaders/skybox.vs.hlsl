// Skybox vertex shader: one triangle that covers the whole screen, built
// from SV_VertexID so no vertex buffer is bound. See skybox.ps.hlsl for the
// resource mapping.

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

VSOutput Main(uint vertex_id : SV_VertexID)
{
    // (-1,-1), (3,-1), (-1,3): the corners outside the screen are clipped.
    float2 ndc = float2((vertex_id << 1) & 2, vertex_id & 2) * 2.0 - 1.0;

    VSOutput output;
    output.position = float4(ndc, 1.0, 1.0);
    output.ndc = ndc;
    return output;
}
