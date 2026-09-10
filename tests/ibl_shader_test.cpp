#include "ibl.h"
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "IBL shader test");
    // Unit white environment gives E=pi and outgoing white radiance=1.
    DiffuseIbl ibl;
    ibl.coefficients[0] = {11.13665599f, 11.13665599f, 11.13665599f};
    Shader shader = load_diffuse_ibl_shader(argv[1], argv[2], ibl);
    if (shader.id == 0) { CloseWindow(); return 1; }
    Model cube = LoadModelFromMesh(GenMeshCube(2, 2, 2));
    cube.materials[0].shader = shader;
    RenderTexture2D target = LoadRenderTexture(64, 64);
    Camera camera = {{0,0,5}, {0,0,0}, {0,1,0}, 45, CAMERA_PERSPECTIVE};
    Image gray = GenImageColor(1, 1, {128,128,128,255});
    Texture2D gray_texture = LoadTextureFromImage(gray);
    UnloadImage(gray);
    const Texture2D white_texture = cube.materials[0].maps[MATERIAL_MAP_ALBEDO].texture;
    struct Case { float exposure; int curve; bool gray; int expected; };
    const Case cases[] = {{0,0,false,188}, {1,0,false,213}, {-1,0,false,156},
        {0,1,false,232}, {0,0,true,117}};
    bool passed = true;
    for (const auto& test : cases) {
    SetShaderValue(shader, GetShaderLocation(shader, "exposureEV"), &test.exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "toneMapping"), &test.curve, SHADER_UNIFORM_INT);
    cube.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = test.gray ? gray_texture : white_texture;
    BeginTextureMode(target);
    ClearBackground(BLACK);
    BeginMode3D(camera);
    DrawModel(cube, {0,0,0}, 1, WHITE);
    EndMode3D();
    EndTextureMode();
    Image image = LoadImageFromTexture(target.texture);
    Color center = GetImageColor(image, 32, 32);
    passed &= std::abs(int(center.r)-test.expected) <= 2 &&
        std::abs(int(center.g)-test.expected) <= 2 && std::abs(int(center.b)-test.expected) <= 2;
    std::printf("IBL shader pixel: %d %d %d (expected %d)\n", center.r, center.g, center.b, test.expected);
    UnloadImage(image);
    }
    UnloadTexture(gray_texture);
    UnloadRenderTexture(target);
    UnloadModel(cube);
    UnloadShader(shader);
    CloseWindow();
    return passed ? 0 : 1;
}
