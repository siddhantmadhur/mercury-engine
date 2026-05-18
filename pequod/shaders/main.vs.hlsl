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

// Light-space view-projection — used to compute the per-vertex
// shadow-map sample position. Lives in its own cbuffer so the shadow VS
// can share it.
cbuffer VS_LIGHT_BUFFER : register(b2)
{
    matrix mLightViewProj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float4 light_space_pos : TEXCOORD3;
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
    float3 scaled_position = input.position * scale * 1.0;
    float4x4 model = {
       1, 0, 0, 0,
       0, 1, 0, 0,
       0, 0, 1, 0,
       0, 0, 0, 1
    };
    model = Rotate(model, degToRad(object_rotation));
    model = mul(Translate(object_position), model);

    float4 world_pos = mul(model, float4(scaled_position, 1.0));
    output.position = mul(mWorldViewProj, world_pos);

    // TODO: switch to inverse-transpose of the upper 3x3 if non-uniform
    // scale becomes a thing. For now the engine only applies uniform-ish
    // scale + rotation so multiplying the rotation matrix is equivalent.
    float4x4 rot_only = Rotate(float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1),
                               degToRad(object_rotation));
    float3 world_normal = normalize(mul((float3x3)rot_only, input.normal));

    output.world_pos = world_pos.xyz;
    output.world_normal = world_normal;
    output.light_space_pos = mul(mLightViewProj, float4(world_pos.xyz, 1.0));

    output.color = float4(input.color, opacity);
    output.uv = atlas_uv.xy + input.uv * (atlas_uv.zw - atlas_uv.xy);
    return output;
}
