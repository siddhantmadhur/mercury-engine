// UI Toolkit vertex shader. Vertices arrive in render-target pixels (origin
// at the top left, y down) and are mapped to clip space here. See ui.ps.hlsl
// for the resource mapping.

struct UIConstants
{
    float2 mInvTargetSize;  // 1 / render target size in pixels
};
[[vk::push_constant]] UIConstants ui;

struct VSInput
{
    // Layout matches Mercury::UI::UIVertex.
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(3)]] float4 outline : COLOR1;
    [[vk::location(4)]] float4 params : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float4 outline : COLOR1;
    [[vk::location(3)]] nointerpolation float4 params : TEXCOORD1;
};

VSOutput Main(VSInput input)
{
    VSOutput output;
    // Vulkan's clip space has +y down, the same as the UI.
    output.position = float4(input.position * ui.mInvTargetSize * 2.0 - 1.0, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    output.outline = input.outline;
    output.params = input.params;
    return output;
}
