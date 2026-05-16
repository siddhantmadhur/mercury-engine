// Handles frame data like camera information
cbuffer VS_CAMERA_BUFFER : register(b0)
{
    matrix mWorldViewProj;
    float2 mResolution;
};

// Handles information on a per-model basis
cbuffer VS_MODEL_BUFFER : register(b1)
{
    float3 scale;
    float opacity;
    float3 object_position;
    float3 object_rotation;
    float4 atlas_uv;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float3 degToRad(float3 deg)
{
    float3 rad = deg;
    rad.x = radians(deg.x);
    rad.y = radians(deg.y);
    rad.z = radians(deg.z);
    return rad;
}

float4x4 RotateX(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float4x4(
        1, 0,  0, 0,
        0, c, -s, 0,
        0, s,  c, 0,
        0, 0,  0, 1
    );
}

// Rotation around Y axis
float4x4 RotateY(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float4x4(
        c, 0, s, 0,
        0, 1, 0, 0,
       -s, 0, c, 0,
        0, 0, 0, 1
    );
}

// Rotation around Z axis
float4x4 RotateZ(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float4x4(
        c, -s, 0, 0,
        s,  c, 0, 0,
        0,  0, 1, 0,
        0,  0, 0, 1
    );
}

float4x4 Rotate(matrix self, float3 rotation) {
    float4x4 rotMat = mul(RotateZ(rotation.z), mul(RotateY(rotation.y), RotateX(rotation.x)));
    matrix m = mul(self, rotMat);
    return m;
}

float4x4 Translate(float3 t) {
    return float4x4(
        1, 0, 0, t.x,
        0, 1, 0, t.y,
        0, 0, 1, t.z,
        0, 0, 0, 1
    );
}

VSOutput Main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    // proj * view * model * pos
    float3 scaled_position = input.position * scale * 1.0;
    float4x4 model = {
       1, 0, 0, 0,
       0, 1, 0, 0,
       0, 0, 1, 0,
       0, 0, 0, 1
    };
     model = Rotate(model, object_rotation);
     model = mul(Translate(object_position), model);



    float4 world_pos = mul(model, float4(scaled_position, 1.0));

    // Snap in world space (before WVP) so exact half-integer coords like
    // ±638.5 don't get perturbed by the projection roundtrip and end up
    // rounding asymmetrically across the origin. Round-half-toward-zero
    // keeps odd-thickness primitives at their intended pixel width.
    //world_pos.xy = sign(world_pos.xy) * ceil(abs(world_pos.xy) - 0.5);

    output.position = mul(mWorldViewProj, world_pos);

    output.color = float4(input.color, opacity);
    output.uv = atlas_uv.xy + input.uv * (atlas_uv.zw - atlas_uv.xy);
    return output;
}