// Vulkan resource mapping (UI descriptor set 0, one per texture). DXC
// derives the bindings from the HLSL registers through the -fvk-*-shift
// flags in CMakeLists.txt:
//   t0 -> binding 10  the batch's texture (white for untextured shapes)
//   s0 -> binding 20  its sampler
//   push constant     1 / render target size (see ui.vs.hlsl)
// Keep this in sync with vk::UIRenderer in vulkan/vk_ui_renderer.cc.

Texture2D<float4> ui_texture : register(t0);
SamplerState      ui_sampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float4 outline : COLOR1;
    // x: mode (0 solid, 1 textured, 2 SDF text), y: SDF edge,
    // z: outline width in SDF units, w: extra softness in SDF units
    [[vk::location(3)]] nointerpolation float4 params : TEXCOORD1;
};

float4 Main(PSInput input) : SV_Target0
{
    const uint mode = (uint)(input.params.x + 0.5);
    if (mode == 0)
    {
        return input.color;
    }
    const float4 texel = ui_texture.Sample(ui_sampler, input.uv);
    if (mode == 1)
    {
        return texel * input.color;
    }

    // Signed distance field glyph: 0.5 is the outline of the glyph.
    const float distance = texel.a;
    const float width = max(fwidth(distance) * 0.7, 1e-4) + input.params.w;
    const float edge = input.params.y;
    const float fill = smoothstep(edge - width, edge + width, distance);
    if (input.params.z <= 0.0)
    {
        return float4(input.color.rgb, input.color.a * fill);
    }
    const float outer_edge = edge - input.params.z;
    const float coverage = smoothstep(outer_edge - width, outer_edge + width, distance);
    const float4 color = lerp(input.outline, input.color, fill);
    return float4(color.rgb, color.a * coverage);
}
