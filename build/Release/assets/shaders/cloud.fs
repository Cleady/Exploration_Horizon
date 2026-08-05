#version 330 core
in vec2 vWorldXZ;
in vec3 vWorldPos;

out vec4 FragColor;

uniform sampler2D cloudTexture;
uniform float uTime;
uniform vec3 uCameraPos;

void main() {
    // 1. Calculate distance from camera in XZ plane for horizon fog fade
    float dist = length(vWorldPos.xz - uCameraPos.xz);
    float fade = 1.0 - smoothstep(450.0f, 950.0f, dist);
    if (fade <= 0.0f) {
        discard;
    }

    // 2. Scale world coordinates for clean tiled blocky mapping
    vec2 cloudUV = (vWorldXZ / 1120.0) + vec2(uTime * 0.001, 0.0);
    
    // 3. Sample from clouds.png
    vec4 texColor = texture(cloudTexture, cloudUV);

    // 4. Discard empty sky pixels
    if (texColor.a < 0.2f) {
        discard;
    }

    // 5. Output white cloud pixel with distance fog blending
    FragColor = vec4(1.0f, 1.0f, 1.0f, 0.85f * fade);
}
