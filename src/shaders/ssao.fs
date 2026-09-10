#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform mat4 projection;
uniform mat4 inverseProjection;
uniform vec2 texelSize;
uniform float radius;

vec3 position(vec2 uv) {
    vec4 p = inverseProjection * vec4(uv * 2.0 - 1.0, texture(texture0, uv).r * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
void main() {
    vec2 uv = fragTexCoord;
    if (texture(texture0, uv).r >= 0.999999) { finalColor = vec4(1.0); return; }
    vec3 p = position(uv);
    // Choose the nearer derivative on each axis to avoid silhouette normals.
    vec3 l = p - position(uv - vec2(texelSize.x, 0));
    vec3 r = position(uv + vec2(texelSize.x, 0)) - p;
    vec3 d = p - position(uv - vec2(0, texelSize.y));
    vec3 u = position(uv + vec2(0, texelSize.y)) - p;
    vec3 n = normalize(cross(abs(l.z) < abs(r.z) ? l : r, abs(d.z) < abs(u.z) ? d : u));
    if (dot(n, -p) < 0.0) n = -n;
    vec3 tangent = normalize(cross(abs(n.z) < 0.99 ? vec3(0,0,1) : vec3(0,1,0), n));
    mat3 basis = mat3(tangent, cross(n, tangent), n);
    float angle = fract(sin(dot(floor(uv / texelSize), vec2(12.9898,78.233))) * 43758.5453) * 6.283185;
    float blocked = 0.0;
    for (int i = 0; i < 16; ++i) {
        float t = (float(i) + 0.5) / 16.0;
        float a = angle + float(i) * 2.399963;
        vec3 direction = vec3(cos(a) * sqrt(1.0-t*t), sin(a) * sqrt(1.0-t*t), t);
        vec3 samplePosition = p + basis * direction * radius * mix(0.15, 1.0, t*t);
        vec4 clip = projection * vec4(samplePosition, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(sampleUV, vec2(0))) || any(greaterThan(sampleUV, vec2(1)))) continue;
        if (texture(texture0, sampleUV).r >= 0.999999) continue;
        float z = position(sampleUV).z;
        float weight = smoothstep(0.0, 1.0, radius / max(abs(p.z-z), 0.0001));
        blocked += (z >= samplePosition.z + radius * 0.025 ? 1.0 : 0.0) * weight;
    }
    finalColor = vec4(vec3(1.0 - blocked / 16.0), 1.0);
}
