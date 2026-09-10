#pragma once

#include "raylib.h"
#include <array>

// Real SH order: 1, y, z, x, xy, yz, 3z*z-1, xz, x*x-y*y.
// Coefficients contain cosine-convolved irradiance E, not E/pi. A Lambertian
// shader must multiply by linear albedo/pi and clamp negative SH ringing.
struct DiffuseIbl {
    std::array<Vector3, 9> coefficients {};
};

// Linear RGB equirectangular panorama: north (+Y) at the top, longitude
// -pi at the left, 0 (+X) at the center, +pi at the right, increasing toward +Z.
DiffuseIbl integrate_diffuse_ibl(const float* rgb, int width, int height);
bool load_diffuse_ibl(const char* path, DiffuseIbl& result);
// Returns an empty shader on failure; caller owns the returned shader.
Shader load_diffuse_ibl_shader(const char* vertex_path, const char* fragment_path, const DiffuseIbl& ibl);
