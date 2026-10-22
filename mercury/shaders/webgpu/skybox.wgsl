struct SkyboxUniforms {
    inv_view_proj: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> skybox: SkyboxUniforms;
@group(0) @binding(1) var panorama: texture_2d<f32>;
@group(0) @binding(2) var panorama_sampler: sampler;

const PI: f32 = 3.14159265;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

// One triangle covering the screen, built from the vertex index.
@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    let ndc = vec2<f32>(f32((vertex_index << 1u) & 2u),
                        f32(vertex_index & 2u)) * 2.0 - 1.0;
    var out: VertexOutput;
    out.position = vec4<f32>(ndc, 1.0, 1.0);
    out.ndc = ndc;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let far_point = skybox.inv_view_proj * vec4<f32>(input.ndc, 1.0, 1.0);
    let dir = normalize(far_point.xyz / far_point.w);

    // Longitude around +Y (the image centre faces -Z), latitude from the top.
    let uv = vec2<f32>(atan2(dir.x, -dir.z) / (2.0 * PI) + 0.5,
                       acos(clamp(dir.y, -1.0, 1.0)) / PI);
    // Explicit LOD: the derivative of uv.x jumps across the seam.
    return vec4<f32>(textureSampleLevel(panorama, panorama_sampler, uv, 0.0).rgb, 1.0);
}
