@ctype vec3 glm::vec3
@ctype vec4 glm::vec4
@ctype mat4 glm::mat4
@header #include <glm/glm.hpp>


@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
};

in vec3 position;
in vec2 texcoord0;
in vec4 color0;
in float use_texture0;

out vec2 uv;
out vec4 color;
out float use_texture; 

void main() {
    gl_Position = mvp * vec4(position, 1);
    uv = texcoord0;
    color = color0;
    use_texture = use_texture0;
}

@end

@fs fs


layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
in float use_texture;

out vec4 FragColor;

void main() {
    if (use_texture > 0.5) {
        FragColor = texture(sampler2D(tex, smp), uv) * color;
    } else {
        FragColor = color;
    }
}

@end

@program simple vs fs
