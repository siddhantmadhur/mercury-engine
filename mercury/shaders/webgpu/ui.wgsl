// UI Toolkit shader. Vertices arrive in render-target pixels (origin at the
// top left, y down). Mirrored in webgpu/wgpu_ui_renderer.cc.

struct UIUniforms {
    inv_target_size: vec2<f32>,
    padding: vec2<f32>,
};

@group(0) @binding(0) var<uniform> ui: UIUniforms;
@group(1) @binding(0) var ui_texture: texture_2d<f32>;
@group(1) @binding(1) var ui_sampler: sampler;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) color: vec4<f32>,
    @location(3) outline: vec4<f32>,
    @location(4) params: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) outline: vec4<f32>,
    // x: mode (0 solid, 1 textured, 2 SDF text), y: SDF edge,
    // z: outline width in SDF units, w: extra softness in SDF units
    @location(3) @interpolate(flat) params: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let ndc = input.position * ui.inv_target_size * 2.0 - 1.0;
    // WebGPU's clip space has +y up.
    out.position = vec4<f32>(ndc.x, -ndc.y, 0.0, 1.0);
    out.uv = input.uv;
    out.color = input.color;
    out.outline = input.outline;
    out.params = input.params;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sampled before branching: derivatives need uniform control flow.
    let texel = textureSample(ui_texture, ui_sampler, input.uv);
    let width = max(fwidth(texel.a) * 0.7, 1e-4) + input.params.w;
    let mode = u32(input.params.x + 0.5);
    if (mode == 0u) {
        return input.color;
    }
    if (mode == 1u) {
        return texel * input.color;
    }
    let edge = input.params.y;
    let fill = smoothstep(edge - width, edge + width, texel.a);
    if (input.params.z <= 0.0) {
        return vec4<f32>(input.color.rgb, input.color.a * fill);
    }
    let outer_edge = edge - input.params.z;
    let coverage = smoothstep(outer_edge - width, outer_edge + width, texel.a);
    let color = mix(input.outline, input.color, fill);
    return vec4<f32>(color.rgb, color.a * coverage);
}
