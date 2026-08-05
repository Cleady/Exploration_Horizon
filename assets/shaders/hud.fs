#version 330 core
in vec2 vUV;

uniform sampler2D uTexture;
uniform vec4 uRectUV;      // icon sub-rect inside the atlas (uMin, vMin, uMax, vMax)
uniform vec4 uColor;       // solid color used when uUseTexture == false
uniform bool uUseTexture;

out vec4 FragColor;

void main() {
    if (uUseTexture) {
        vec2 uv = uRectUV.xy + vUV * (uRectUV.zw - uRectUV.xy);
        vec4 texColor = texture(uTexture, uv);

        // Same alpha cutout as the world shader, so the hotbar's translucent
        // icons (e.g. LEAVES) don't draw an opaque box over the slot frame.
        if (texColor.a < 0.1) {
            discard;
        }
        FragColor = texColor;
    } else {
        FragColor = uColor;
    }
}
