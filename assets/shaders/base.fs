#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
in float vAlpha;

uniform sampler2D ourTexture;
// Per-draw colour tint multiplied into the final RGB.
// Default (1,1,1,1) = no tint. Set to a deep blue before the water pass.
uniform vec4 uColorTint;

void main() {
    vec4 texColor = texture(ourTexture, TexCoord);

    // Per-vertex alpha: opaque blocks = 1.0, inner leaves = 0.75, water = 0.75.
    float alpha = texColor.a * vAlpha;

    // Cutout threshold 0.1: fully transparent texels (cross-sprite holes,
    // leaf texture gaps) are discarded. Water (vAlpha=0.75) passes here.
    if (alpha < 0.1) {
        discard;
    }

    // Apply colour tint (water = deep blue, everything else = 1,1,1,1).
    vec3 tinted = texColor.rgb * uColorTint.rgb * (0.85 + 0.15 * vAlpha);
    FragColor = vec4(tinted, alpha);
}
