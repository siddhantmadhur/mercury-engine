struct ShadowUniforms {
    light_view_proj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> shadow_uniforms: ShadowUniforms;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(3) inst_scale: vec3<f32>,
    @location(5) inst_world0: vec4<f32>,
    @location(6) inst_world1: vec4<f32>,
    @location(7) inst_world2: vec4<f32>,
    @location(8) inst_world3: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4<f32> {
    let world = mat4x4<f32>(
        input.inst_world0,
        input.inst_world1,
        input.inst_world2,
        input.inst_world3
    );
    let scaled_pos = input.position * input.inst_scale;
    let world_pos = world * vec4<f32>(scaled_pos, 1.0);
    return shadow_uniforms.light_view_proj * world_pos;
}
