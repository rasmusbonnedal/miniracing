#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D depthTexture;
uniform sampler2D aoTexture;
uniform mat4 inverseProjection;
uniform vec2 texelSize;
uniform float radius;
uniform float strength;
float viewZ(vec2 uv) {
    vec4 p = inverseProjection * vec4(uv * 2.0 - 1.0, texture(depthTexture, uv).r * 2.0 - 1.0, 1);
    return p.z / p.w;
}
void main() {
    vec4 color = texture(texture0, fragTexCoord);
    if (texture(depthTexture, fragTexCoord).r >= 0.999999) { finalColor = color; return; }
    float z = viewZ(fragTexCoord);
    float ao = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
        vec2 uv = fragTexCoord + vec2(x,y) * texelSize;
        float weight = exp(-float(x*x+y*y) / 4.0) * exp(-abs(viewZ(uv)-z) / max(radius * 0.1, 0.0001));
        ao += texture(aoTexture, uv).r * weight;
        total += weight;
    }
    // Basic post-lighting approximation; the scene is already tone mapped.
    finalColor = vec4(color.rgb * pow(clamp(ao / total, 0.0, 1.0), strength), color.a);
}
