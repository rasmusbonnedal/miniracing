#include "ssao.h"
#include "raymath.h"
#include "rlgl.h"

bool load_ssao(Ssao& s, const char* evaluate_path, const char* composite_path) {
    s.evaluate = LoadShader(nullptr, evaluate_path);
    s.composite = LoadShader(nullptr, composite_path);
    if (s.evaluate.id == rlGetShaderIdDefault() || s.composite.id == rlGetShaderIdDefault()) {
        unload_ssao(s);
        return false;
    }
    return true;
}

bool resize_ssao(Ssao& s, int width, int height) {
    if (!s.evaluate.id || width <= 0 || height <= 0) return false;
    if (s.scene.id && s.scene.texture.width == width && s.scene.texture.height == height) return true;
    UnloadRenderTexture(s.scene);
    UnloadRenderTexture(s.occlusion);
    s.scene = {};
    s.occlusion = {};
    s.scene.id = rlLoadFramebuffer();
    s.scene.texture = {rlLoadTexture(nullptr, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1),
        width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    s.scene.depth = {rlLoadTextureDepth(width, height, false), width, height, 1, 0};
    rlFramebufferAttach(s.scene.id, s.scene.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(s.scene.id, s.scene.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    const bool complete = rlFramebufferComplete(s.scene.id);
    rlDisableFramebuffer();
    s.occlusion = LoadRenderTexture(width, height);
    if (!complete || !IsRenderTextureValid(s.occlusion)) {
        UnloadRenderTexture(s.scene);
        UnloadRenderTexture(s.occlusion);
        s.scene = {};
        s.occlusion = {};
        return false;
    }
    SetTextureWrap(s.scene.depth, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(s.scene.depth, TEXTURE_FILTER_POINT);
    SetTextureWrap(s.occlusion.texture, TEXTURE_WRAP_CLAMP);
    return true;
}

void draw_ssao(const Ssao& s, Matrix projection, float radius, float strength) {
    const Vector2 texel = {1.0f / s.scene.texture.width, 1.0f / s.scene.texture.height};
    const Matrix inverse = MatrixInvert(projection);
    auto setup = [&](Shader shader) {
        SetShaderValueMatrix(shader, GetShaderLocation(shader, "inverseProjection"), inverse);
        SetShaderValue(shader, GetShaderLocation(shader, "texelSize"), &texel, SHADER_UNIFORM_VEC2);
        SetShaderValue(shader, GetShaderLocation(shader, "radius"), &radius, SHADER_UNIFORM_FLOAT);
    };
    setup(s.evaluate);
    SetShaderValueMatrix(s.evaluate, GetShaderLocation(s.evaluate, "projection"), projection);
    BeginTextureMode(s.occlusion);
    ClearBackground(WHITE);
    BeginShaderMode(s.evaluate);
    DrawTextureRec(s.scene.depth, {0, 0, float(s.scene.texture.width), -float(s.scene.texture.height)}, {0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    setup(s.composite);
    SetShaderValue(s.composite, GetShaderLocation(s.composite, "strength"), &strength, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(s.composite);
    SetShaderValueTexture(s.composite, GetShaderLocation(s.composite, "depthTexture"), s.scene.depth);
    SetShaderValueTexture(s.composite, GetShaderLocation(s.composite, "aoTexture"), s.occlusion.texture);
    DrawTexturePro(s.scene.texture, {0, 0, float(s.scene.texture.width), -float(s.scene.texture.height)},
        {0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

void unload_ssao(Ssao& s) {
    UnloadRenderTexture(s.scene);
    UnloadRenderTexture(s.occlusion);
    if (s.evaluate.id) UnloadShader(s.evaluate);
    if (s.composite.id) UnloadShader(s.composite);
    s = {};
}
