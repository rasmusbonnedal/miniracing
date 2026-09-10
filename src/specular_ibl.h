#pragma once
#include "raylib.h"
#include <vector>

// Padded equirectangular slices, from mirror (0) to fully rough (1).
struct SpecularIblData {
    int width = 128, height = 64, levels = 6;
    std::vector<float> atlas;
    int lut_size = 64;
    std::vector<float> brdf;
};

// Probability masses are luminance * exact texel solid angle, with a small
// uniform-sphere mixture to retain support in dark regions. Last entry is 1.
std::vector<double> environment_cdf(const float* rgb, int width, int height);
SpecularIblData prefilter_specular_ibl(const float* rgb, int width, int height,
    int output_width = 128, int samples = 256);

struct SpecularIbl {
    Texture2D atlas {}, brdf {};
};
bool load_specular_ibl(const char* path, Shader shader, SpecularIbl& result);
void attach_specular_ibl(Model& model, const SpecularIbl& ibl);
// Raylib does not upload material roughness/metallic values itself.
void draw_ibl_model(const Model& model, Vector3 position, float rotation,
    Vector3 scale, Color tint, bool wires = false);
