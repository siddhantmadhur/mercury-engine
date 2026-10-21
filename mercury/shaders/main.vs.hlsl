// Vulkan resource mapping (descriptor set 0). DXC derives the bindings from
// the HLSL registers through the -fvk-*-shift flags in CMakeLists.txt:
//   b0 -> binding 0   camera uniform buffer         (vertex stage)
//   t0 -> binding 10  atlas sampled image           (fragment stage)
//   s0 -> binding 20  atlas sampler                 (fragment stage)
// Keep this in sync with the descriptor set layout in vulkan_application.cc.

// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

struct VSInput
{
    // Per-vertex data (vertex buffer binding 0, layout matches Vertex)
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;

    // Per-instance data (vertex buffer binding 1, layout matches VsModelBuffer)
    [[vk::location(3)]] float3 inst_scale     : INSTANCE_SCALE;
    [[vk::location(4)]] float  inst_opacity   : INSTANCE_OPACITY;
    [[vk::location(5)]] float4 inst_world0    : INSTANCE_WORLD0;
    [[vk::location(6)]] float4 inst_world1    : INSTANCE_WORLD1;
    [[vk::location(7)]] float4 inst_world2    : INSTANCE_WORLD2;
    [[vk::location(8)]] float4 inst_world3    : INSTANCE_WORLD3;
    [[vk::location(9)]] float4 inst_atlas_uv  : INSTANCE_ATLAS_UV;
};

struct VSOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    // Reconstruct the per-instance world matrix from four float4 inputs.
    // Each input is 16 consecutive bytes of the glm::mat4 stored in the
    // instance buffer. glm is column-major, so those bytes are the glm
    // *columns*. float4x4(a,b,c,d) places each argument as a row, so
    // `world` ends up as the transpose of the original logical matrix.
    // We compensate by multiplying with the vector on the left
    // (mul(v, world) == world^T * v), which restores glm * v.
    matrix world = matrix(input.inst_world0,
                          input.inst_world1,
                          input.inst_world2,
                          input.inst_world3);

    float3 scaled_position = input.position * input.inst_scale * 1.0;

    float4 world_pos = mul(float4(scaled_position, 1.0), world);

    // Snap in world space (before WVP) so exact half-integer coords like
    // ±638.5 don't get perturbed by the projection roundtrip and end up
    // rounding asymmetrically across the origin. Round-half-toward-zero
    // keeps odd-thickness primitives at their intended pixel width.
    //world_pos.xy = sign(world_pos.xy) * ceil(abs(world_pos.xy) - 0.5);

    output.position = mul(mWorldViewProj, world_pos);

    output.color = float4(input.color, input.inst_opacity);
    output.uv = input.inst_atlas_uv.xy
              + input.uv * (input.inst_atlas_uv.zw - input.inst_atlas_uv.xy);
    return output;
}
