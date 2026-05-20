// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;

    // Per-instance data (input slot 1)
    float3 inst_scale     : INSTANCE_SCALE;
    float  inst_opacity   : INSTANCE_OPACITY;
    float4 inst_world0    : INSTANCE_WORLD0;
    float4 inst_world1    : INSTANCE_WORLD1;
    float4 inst_world2    : INSTANCE_WORLD2;
    float4 inst_world3    : INSTANCE_WORLD3;
    float4 inst_atlas_uv  : INSTANCE_ATLAS_UV;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    // Reconstruct the per-instance world matrix from four float4 inputs.
    // Each input is 16 consecutive bytes from the instance buffer, which
    // holds a DirectX::XMFLOAT4X4 (row-major). Because glm is column-major
    // and was memcpy'd directly into XMFLOAT4X4, those bytes are actually
    // the glm *columns*. float4x4(a,b,c,d) places each argument as a row,
    // so `world` ends up as the transpose of the original logical matrix.
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
