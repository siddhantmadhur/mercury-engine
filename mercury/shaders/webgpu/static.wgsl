struct CameraUniforms {
    world_view_proj: mat4x4<f32>,
    light_view_proj: mat4x4<f32>,
    light_dir: vec4<f32>,
    light_color: vec4<f32>,
    ambient_color: vec4<f32>,
    camera_pos: vec4<f32>,
    resolution: vec2<f32>,
    shadow_params: vec2<f32>,
};

@group(0) @binding(0) var<uniform> camera: CameraUniforms;

struct StaticVertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec4<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) atlas_uv: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) world_position: vec3<f32>,
    @location(4) material_params: vec2<f32>,
    // The texture's rectangle in the atlas (x0, y0, x1, y1); uv repeats
    // inside it, so tiling never bleeds into neighbouring images.
    @location(5) @interpolate(flat) atlas_rect: vec4<f32>,
};

@vertex
fn vs_main(input: StaticVertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = camera.world_view_proj * vec4<f32>(input.position, 1.0);
    out.color = input.color;
    out.uv = input.uv;
    out.atlas_rect = input.atlas_uv;
    out.normal = vec3<f32>(0.0, 0.0, 0.0);
    out.world_position = input.position;
    out.material_params = vec2<f32>(0.5, 0.0);
    return out;
}
