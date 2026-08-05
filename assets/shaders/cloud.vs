#version 330 core
layout (location = 0) in vec3 aPos;

out vec2 vWorldXZ;
out vec3 vWorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldXZ = worldPos.xz;
    vWorldPos = worldPos.xyz;
    gl_Position = projection * view * worldPos;
}
