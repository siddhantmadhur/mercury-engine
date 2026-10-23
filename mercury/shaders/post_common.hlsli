// Shared by the fullscreen passes of the model renderer
// (vk_scene_renderer.cc). Descriptor set 0:
//   binding 0  linear clamp sampler
//   binding 1  point clamp sampler
//   binding 2..7  input images (which ones a pass reads is listed there)
// Push constants: PostParams (128 bytes).

[[vk::binding(0, 0)]] SamplerState linear_clamp;
[[vk::binding(1, 0)]] SamplerState point_clamp;
[[vk::binding(2, 0)]] Texture2D input0;
[[vk::binding(3, 0)]] Texture2D input1;
[[vk::binding(4, 0)]] Texture2D input2;
[[vk::binding(5, 0)]] Texture2D input3;
[[vk::binding(6, 0)]] Texture2D input4;
[[vk::binding(7, 0)]] Texture2D input5;

struct PostParams
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4;
    float4 p5;
    float4 p6;
    float4 p7;
};
[[vk::push_constant]] PostParams params;

struct FullscreenOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float3 SrgbToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}
