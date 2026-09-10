#include "ibl.h"
#include "rlgl.h"

#include <cmath>
#include <numbers>

Shader load_diffuse_ibl_shader(const char* vertex_path, const char* fragment_path, const DiffuseIbl& ibl) {
    Shader shader = LoadShader(vertex_path, fragment_path);
    if (shader.id == 0 || shader.id == rlGetShaderIdDefault()) return {};
    const int location = GetShaderLocation(shader, "irradianceSH[0]");
    if (location < 0 || shader.locs[SHADER_LOC_MATRIX_MODEL] < 0 ||
        shader.locs[SHADER_LOC_MATRIX_NORMAL] < 0) {
        TraceLog(LOG_WARNING, "IBL: Missing required shader uniforms");
        UnloadShader(shader);
        return {};
    }
    float values[27];
    for (int i = 0; i < 9; ++i) {
        values[i * 3] = ibl.coefficients[i].x;
        values[i * 3 + 1] = ibl.coefficients[i].y;
        values[i * 3 + 2] = ibl.coefficients[i].z;
    }
    SetShaderValueV(shader, location, values, SHADER_UNIFORM_VEC3, 9);
    return shader;
}

DiffuseIbl integrate_diffuse_ibl(const float* rgb, int width, int height) {
    DiffuseIbl result;
    if (!rgb || width <= 0 || height <= 0) return result;

    constexpr double pi = std::numbers::pi;
    double sums[9][3] {};
    const double longitude_step = 2.0 * pi / width;
    for (int row = 0; row < height; ++row) {
        const double theta = pi * (row + 0.5) / height;
        // Exact texel solid angle avoids overweighting the panorama's poles.
        const double solid_angle = longitude_step *
            (std::cos(pi * row / height) - std::cos(pi * (row + 1) / height));
        for (int col = 0; col < width; ++col) {
            const double phi = longitude_step * (col + 0.5) - pi;
            const double x = std::sin(theta) * std::cos(phi);
            const double y = std::cos(theta);
            const double z = std::sin(theta) * std::sin(phi);
            const double basis[9] = {
                std::sqrt(1.0 / (4.0 * pi)),
                std::sqrt(3.0 / (4.0 * pi)) * y,
                std::sqrt(3.0 / (4.0 * pi)) * z,
                std::sqrt(3.0 / (4.0 * pi)) * x,
                std::sqrt(15.0 / (4.0 * pi)) * x * y,
                std::sqrt(15.0 / (4.0 * pi)) * y * z,
                std::sqrt(5.0 / (16.0 * pi)) * (3.0 * z * z - 1.0),
                std::sqrt(15.0 / (4.0 * pi)) * x * z,
                std::sqrt(15.0 / (16.0 * pi)) * (x * x - y * y)
            };
            const float* pixel = rgb + (static_cast<size_t>(row) * width + col) * 3;
            for (int i = 0; i < 9; ++i)
                for (int channel = 0; channel < 3; ++channel)
                    sums[i][channel] += pixel[channel] * basis[i] * solid_angle;
        }
    }
    for (int i = 0; i < 9; ++i) {
        // Zonal clamped-cosine convolution factors for SH bands 0, 1, and 2.
        const double kernel = i == 0 ? pi : (i < 4 ? 2.0 * pi / 3.0 : pi / 4.0);
        result.coefficients[i] = { static_cast<float>(sums[i][0] * kernel),
            static_cast<float>(sums[i][1] * kernel), static_cast<float>(sums[i][2] * kernel) };
    }
    return result;
}

bool load_diffuse_ibl(const char* path, DiffuseIbl& result) {
    Image image = LoadImage(path);
    if (!image.data) return false;
    // Do not use LoadImageColors: its 8-bit conversion would destroy HDR values.
    if (image.format != PIXELFORMAT_UNCOMPRESSED_R32G32B32 ||
        image.width <= 0 || image.height <= 0 || image.width != 2 * image.height) {
        TraceLog(LOG_WARNING, "IBL: Expected a 2:1 floating-point RGB HDR panorama: %s", path);
        UnloadImage(image);
        return false;
    }
    const float* rgb = static_cast<const float*>(image.data);
    const size_t count = static_cast<size_t>(image.width) * image.height * 3;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(rgb[i]) || rgb[i] < 0.0f) {
            TraceLog(LOG_WARNING, "IBL: Invalid radiance in %s", path);
            UnloadImage(image);
            return false;
        }
    }
    result = integrate_diffuse_ibl(rgb, image.width, image.height);
    TraceLog(LOG_INFO, "IBL: Integrated %s (%dx%d) into 9 diffuse SH coefficients",
        path, image.width, image.height);
    UnloadImage(image);
    return true;
}
