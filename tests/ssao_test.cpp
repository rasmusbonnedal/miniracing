#include "ssao.h"
#include "rlgl.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(128, 128, "SSAO test");
    Ssao s;
    bool passed = load_ssao(s, argv[1], argv[2]) && resize_ssao(s, 64, 64) && resize_ssao(s, 128, 128);
    if (!passed) { unload_ssao(s); CloseWindow(); return 1; }
    Camera camera = {{4,4,6}, {0,0,0}, {0,1,0}, 45, CAMERA_PERSPECTIVE};
    BeginDrawing();
    BeginTextureMode(s.scene);
    ClearBackground(WHITE);
    BeginMode3D(camera);
    Matrix projection = rlGetMatrixProjection();
    DrawPlane({0,0,0}, {20,20}, WHITE);
    DrawCube({0,1,0}, 2, 2, 2, WHITE);
    EndMode3D();
    EndTextureMode();
    draw_ssao(s, projection, 1.0f, 1.0f);
    EndDrawing();
    Image ao = LoadImageFromTexture(s.occlusion.texture);
    int minimum = 255, maximum = 0;
    for (int y = 0; y < ao.height; ++y) for (int x = 0; x < ao.width; ++x) {
        const int value = GetImageColor(ao, x, y).r;
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
    }
    std::printf("SSAO range: %d..%d\n", minimum, maximum);
    passed &= minimum < 240 && minimum > 0 && maximum == 255;
    UnloadImage(ao);
    // Empty depth must produce neutral AO, including after resizing.
    passed &= resize_ssao(s, 96, 96);
    BeginDrawing();
    BeginTextureMode(s.scene);
    ClearBackground(WHITE);
    EndTextureMode();
    draw_ssao(s, projection, 1.0f, 1.0f);
    EndDrawing();
    ao = LoadImageFromTexture(s.occlusion.texture);
    for (int y = 0; y < ao.height; ++y) for (int x = 0; x < ao.width; ++x)
        passed &= GetImageColor(ao, x, y).r == 255;
    UnloadImage(ao);
    unload_ssao(s);
    CloseWindow();
    return passed ? 0 : 1;
}
