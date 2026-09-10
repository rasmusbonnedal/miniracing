#version 330

in vec3 worldPosition;
in vec3 worldNormal;
in vec2 texCoord;
in vec4 color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 irradianceSH[9];
uniform int useGeometricNormal;
uniform float exposureEV;
uniform int toneMapping;
uniform int specularEnabled;
uniform sampler2D specularAtlas;
uniform sampler2D brdfLut;
uniform vec3 specularLayout;
uniform vec3 cameraPosition;
uniform float roughness;
uniform float metallic;
out vec4 finalColor;

vec3 srgbToLinear(vec3 c)
{
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92,
        lessThanEqual(c, vec3(0.04045)));
}

vec3 linearToSrgb(vec3 c)
{
    return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c,
        lessThanEqual(c, vec3(0.0031308)));
}

vec3 specularLevel(vec2 uv, float level)
{
    vec2 size = specularLayout.xy;
    // Guard bands interpolate across the longitude seam without bleeding
    // into neighboring roughness slices; poles clamp to the last row center.
    uv.y = clamp(uv.y, 0.5 / size.y, 1.0 - 0.5 / size.y);
    vec2 pixel = vec2(1.0) + uv * size;
    pixel.y += level * (size.y + 2.0);
    return texture(specularAtlas, pixel / vec2(size.x + 2.0,
        (size.y + 2.0) * specularLayout.z)).rgb;
}

vec3 specularEnvironment(vec3 r)
{
    vec2 uv = vec2(fract(atan(r.z, r.x) / 6.283185307179586 + 0.5),
        acos(clamp(r.y, -1.0, 1.0)) / 3.141592653589793);
    float level = roughness * (specularLayout.z - 1.0);
    return mix(specularLevel(uv, floor(level)),
        specularLevel(uv, min(floor(level) + 1.0, specularLayout.z - 1.0)), fract(level));
}

void main()
{
    // Optional geometric fallback for meshes without normals.
    vec3 geometricNormal = cross(dFdx(worldPosition), dFdy(worldPosition));
    if (!gl_FrontFacing) geometricNormal = -geometricNormal;
    vec3 n = normalize(useGeometricNormal != 0 ? geometricNormal : worldNormal);
    // Same real SH basis/order as the CPU's cosine-convolved coefficients.
    vec3 irradiance = irradianceSH[0] * 0.282094791773878
        + irradianceSH[1] * (0.48860251190292 * n.y)
        + irradianceSH[2] * (0.48860251190292 * n.z)
        + irradianceSH[3] * (0.48860251190292 * n.x)
        + irradianceSH[4] * (1.09254843059208 * n.x * n.y)
        + irradianceSH[5] * (1.09254843059208 * n.y * n.z)
        + irradianceSH[6] * (0.31539156525252 * (3.0 * n.z * n.z - 1.0))
        + irradianceSH[7] * (1.09254843059208 * n.x * n.z)
        + irradianceSH[8] * (0.54627421529604 * (n.x * n.x - n.y * n.y));
    vec4 texel = texture(texture0, texCoord);
    // glTF material factors and vertex colors are linear; base-color textures are sRGB.
    vec3 albedo = srgbToLinear(texel.rgb) * colDiffuse.rgb * color.rgb;
    vec3 radiance = albedo * max(irradiance, vec3(0.0)) / 3.141592653589793;
    if (specularEnabled != 0) {
        vec3 view = normalize(cameraPosition - worldPosition);
        float noV = max(dot(n, view), 0.0001);
        vec3 f0 = mix(vec3(0.04), albedo, metallic);
        vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - noV, 5.0);
        // Metals contribute no diffuse; dielectric Fresnel reserves energy for specular.
        radiance *= (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec2 brdf = texture(brdfLut, vec2(noV, roughness)).rg;
        radiance += specularEnvironment(reflect(-view, n)) * (f0 * brdf.x + brdf.y);
    }
    radiance *= exp2(exposureEV);
    vec3 mapped;
    if (toneMapping == 1) {
        // ACES fitted curve approximation, not the full ACES color pipeline.
        mapped = clamp((radiance * (2.51 * radiance + 0.03)) /
            (radiance * (2.43 * radiance + 0.59) + 0.14), 0.0, 1.0);
    } else {
        mapped = radiance / (vec3(1.0) + radiance);
    }
    finalColor = vec4(linearToSrgb(mapped), texel.a * colDiffuse.a * color.a);
}
