struct CameraUniforms {
    world_view_proj: mat4x4<f32>,
    light_view_proj: mat4x4<f32>,
    light_dir: vec4<f32>,       // xyz: direction light travels
    light_color: vec4<f32>,     // rgb: light color * intensity
    ambient_color: vec4<f32>,   // rgb: ambient color
    camera_pos: vec4<f32>,      // xyz: camera world position
    resolution: vec2<f32>,      // screen resolution
    shadow_params: vec2<f32>,   // x: shadow resolution, y: normal bias
};

@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@group(0) @binding(1) var diffuse_texture: texture_2d<f32>;
@group(0) @binding(2) var diffuse_sampler: sampler;
@group(0) @binding(3) var shadow_map: texture_depth_2d;
@group(0) @binding(4) var shadow_sampler: sampler_comparison;

struct VertexInput {
    // Binding 0: per-vertex
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(10) normal: vec3<f32>,

    // Binding 1: per-instance
    @location(3) inst_scale: vec3<f32>,
    @location(4) inst_opacity: f32,
    @location(5) inst_world0: vec4<f32>,
    @location(6) inst_world1: vec4<f32>,
    @location(7) inst_world2: vec4<f32>,
    @location(8) inst_world3: vec4<f32>,
    @location(9) inst_atlas_uv: vec4<f32>,
    @location(11) inst_material: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) normal: vec3<f32>,
    @location(3) world_position: vec3<f32>,
    @location(4) material_params: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let world = mat4x4<f32>(
        input.inst_world0,
        input.inst_world1,
        input.inst_world2,
        input.inst_world3
    );

    let scaled_pos = input.position * input.inst_scale;
    let world_pos = world * vec4<f32>(scaled_pos, 1.0);

    out.position = camera.world_view_proj * world_pos;
    out.world_position = world_pos.xyz;
    out.color = vec4<f32>(input.color, input.inst_opacity);
    out.uv = input.inst_atlas_uv.xy + input.uv * input.inst_atlas_uv.zw;
    out.material_params = input.inst_material.xy;

    let norm_len = length(input.normal);
    if (norm_len > 0.001) {
        let n = (world * vec4<f32>(input.normal, 0.0)).xyz;
        out.normal = normalize(n);
    } else {
        out.normal = vec3<f32>(0.0, 0.0, 0.0);
    }

    return out;
}

const PI: f32 = 3.14159265358979323846;

fn DistributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;
    let denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

fn GeometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
}

fn GeometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    let ggx2 = GeometrySchlickGGX(NdotV, roughness);
    let ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

fn FresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (vec3<f32>(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn FresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    let max_f = max(vec3<f32>(1.0 - roughness), F0);
    return F0 + (max_f - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn CalculateShadow(world_pos: vec3<f32>, normal: vec3<f32>, to_light: vec3<f32>) -> f32 {
    if (camera.shadow_params.x <= 0.0) {
        return 1.0;
    }

    let NdotL = max(dot(normal, to_light), 0.0);
    if (NdotL <= 0.001) {
        return 0.0;
    }

    let normal_bias = max(camera.shadow_params.y, 0.02) * (1.0 - NdotL * 0.7);
    let biased_pos = world_pos + normal * normal_bias;
    let light_clip = camera.light_view_proj * vec4<f32>(biased_pos, 1.0);
    let proj = light_clip.xyz / light_clip.w;

    let shadow_uv = vec2<f32>(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    if (shadow_uv.x < 0.001 || shadow_uv.x > 0.999 ||
        shadow_uv.y < 0.001 || shadow_uv.y > 0.999 ||
        proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }

    let current_depth = proj.z;
    let texel = 1.0 / max(camera.shadow_params.x, 1024.0);

    var poisson_disk = array<vec2<f32>, 16>(
        vec2<f32>(-0.94201624, -0.39906216),
        vec2<f32>( 0.94558609, -0.76890725),
        vec2<f32>(-0.09418410, -0.92938870),
        vec2<f32>( 0.34495938,  0.29387760),
        vec2<f32>(-0.91588581,  0.45771432),
        vec2<f32>(-0.81544232, -0.87912464),
        vec2<f32>(-0.38277543,  0.27676845),
        vec2<f32>( 0.97484398,  0.75648379),
        vec2<f32>( 0.44323325, -0.97511554),
        vec2<f32>( 0.53742981, -0.47373420),
        vec2<f32>(-0.26496911, -0.41893023),
        vec2<f32>( 0.79197514,  0.19090188),
        vec2<f32>(-0.24188840,  0.99706507),
        vec2<f32>(-0.81409955,  0.91437590),
        vec2<f32>( 0.19984126,  0.78641367),
        vec2<f32>( 0.14383161, -0.14100790)
    );

    var shadow: f32 = 0.0;
    for (var i: i32 = 0; i < 16; i = i + 1) {
        let offset = poisson_disk[i] * (texel * 1.5);
        shadow = shadow + textureSampleCompareLevel(
            shadow_map,
            shadow_sampler,
            shadow_uv + offset,
            current_depth
        );
    }
    return shadow / 16.0;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let base = textureSample(diffuse_texture, diffuse_sampler, input.uv) * input.color;

    let normal_len = length(input.normal);
    if (normal_len < 0.001) {
        return base;
    }

    let N = input.normal / normal_len;
    let V = normalize(camera.camera_pos.xyz - input.world_position);
    let to_light = -normalize(camera.light_dir.xyz);
    let H = normalize(V + to_light);

    let roughness = clamp(input.material_params.x, 0.04, 1.0);
    let metallic = clamp(input.material_params.y, 0.0, 1.0);

    let albedo = base.rgb;
    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    let NdotL = max(dot(N, to_light), 0.0);
    let NdotV = max(dot(N, V), 1e-4);

    let D = DistributionGGX(N, H, roughness);
    let G = GeometrySmith(N, V, to_light, roughness);
    let F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    let kS = F;
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

    let diffuse = kD * (albedo / PI);
    let specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

    let shadow = CalculateShadow(input.world_position, N, to_light);
    let direct = (diffuse + specular) * camera.light_color.rgb * NdotL * shadow;

    let hemi = dot(N, vec3<f32>(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    let sky_ambient = camera.ambient_color.rgb;
    let ground_ambient = camera.ambient_color.rgb * 0.35;
    let ambient_light = mix(ground_ambient, sky_ambient, hemi);

    let F_ambient = FresnelSchlickRoughness(NdotV, F0, roughness);
    let kD_ambient = (vec3<f32>(1.0) - F_ambient) * (1.0 - metallic);
    let ambient_diffuse = kD_ambient * albedo * ambient_light;
    let ambient_specular = F_ambient * ambient_light * (1.0 - roughness * 0.5);
    let ambient = ambient_diffuse + ambient_specular;

    let lit = direct + ambient;

    // ACES Filmic Tone Mapping
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    let mapped = clamp((lit * (a * lit + b)) / (lit * (c * lit + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));

    // sRGB Gamma Correction
    let gamma_corrected = pow(mapped, vec3<f32>(1.0 / 2.2));

    return vec4<f32>(gamma_corrected, base.a);
}
