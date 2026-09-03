#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0; // raylib's default sampler name; bound to the accum RenderTexture2D
uniform vec2 u_resolution;
uniform float u_sampleCount;

const float EXPOSURE = 1.05;

void main() {
    // texture0 holds the SUM of all accumulated samples (float render target,
    // additive-blended). Divide by how many samples have landed in it so far
    // to get the running average -> this is the actual "accumulation" step.
    vec3 col = texture(texture0, fragTexCoord).rgb / max(u_sampleCount, 1.0);

    col *= EXPOSURE;

    vec3 bloom = smoothstep(0.8, 2.0, col) * 0.3;
    col += bloom;

    vec2 uv = fragTexCoord * 2.0 - 1.0;
    uv.x *= u_resolution.x / u_resolution.y;
    float vig = 1.0 - 0.2 * dot(uv, uv);
    col *= vig;

    finalColor = vec4(col, 1.0);
}