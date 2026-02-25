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

out vec2 uv;

void main() {
    gl_Position = mvp * vec4(position, 1);
    uv = texcoord0;
}

@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
out vec4 FragColor;

void main() {
    FragColor = texture(sampler2D(tex, smp), uv); 
    //FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}

@end

@program simple vs fs
