// Basic unlit/textured model pixel shader (no lighting, GI, AO, or shadows).
#include "model_common.hlsli"

[[vk::binding(3, 0)]] SamplerState material_sampler;
[[vk::binding(4, 0)]] SamplerState clamp_sampler;
[[vk::binding(0, 1)]] Texture2D textures[];

float4 Main(ModelVSOutput input) : SV_Target0
{
    DrawData data = draws[input.draw];
    const uint draw_flags = data.info.y;
    const uint texture_index = data.info.x;

    float4 texel = (draw_flags & kFlagClampTexture) != 0
        ? textures[NonUniformResourceIndex(texture_index)].Sample(clamp_sampler, input.uv)
        : textures[NonUniformResourceIndex(texture_index)].Sample(material_sampler, input.uv);

    float4 base = input.color;
    if ((draw_flags & kFlagSdfText) != 0)
    {
        float distance = texel.a;
        float width = max(fwidth(distance) * 0.75, 1e-4);
        base.a *= smoothstep(0.5 - width, 0.5 + width, distance);
        if (base.a < 0.004) discard;
    }
    else
    {
        base *= texel;
    }

    float3 rgb = flags.x > 0.5 ? LinearToSrgb(base.rgb) : base.rgb;
    return float4(saturate(rgb), base.a);
}
