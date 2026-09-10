#pragma once
#include "raylib.h"

// Owns a scene color/depth target and a full-resolution occlusion target.
struct Ssao {
    RenderTexture2D scene {};
    RenderTexture2D occlusion {};
    Shader evaluate {};
    Shader composite {};
};
bool load_ssao(Ssao& ssao, const char* evaluate_path, const char* composite_path);
bool resize_ssao(Ssao& ssao, int width, int height);
void draw_ssao(const Ssao& ssao, Matrix projection, float radius, float strength);
void unload_ssao(Ssao& ssao);
