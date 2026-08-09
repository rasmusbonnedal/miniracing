#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "rlImGui.h"
#include "imgui.h"

#include <cmath>
#include <filesystem>
#include <map>
#include <vector>
#include <iostream>

int main() {
    const int screenWidth = 1920;
    const int screenHeight = 1080;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Level Editor");
    SetTargetFPS(60);

    std::map<std::string, Model> models;
    std::map<std::string, Texture2D> icons;
    try {
        for (const auto& f : std::filesystem::directory_iterator(MODEL_PATH "Isometric")) {
            if (f.is_regular_file() && f.path().string().ends_with("NE.png")) {
                icons[f.path().stem().string()] = LoadTexture(f.path().string().c_str());
            }
        }
        for (const auto& f : std::filesystem::directory_iterator(MODEL_PATH "Models/GLTF Format")) {
            if (f.is_regular_file()) {
                models[f.path().stem().string()] = LoadModel(f.path().string().c_str());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    Camera3D camera = {0};
    camera.position = {4.0f, 4.0f, 4.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rlImGuiSetup(true);

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        UpdateCamera(&camera, CAMERA_ORBITAL);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);
        DrawGrid(10, 1.0f);
        
        int cols = sqrt(models.size() + 1);
        int rows = (models.size() + cols - 1) / cols;
        auto it = models.begin();
        for (int x = 0; x < cols; x++) {
            for (int y = 0; y < rows; y++) {
                int index = y * cols + x;
                if (index >= models.size()) break;
                const Vector3 pos = {(x - cols / 2) * 2.0f, 0.0f, (y - rows / 2) * 2.0f};
                DrawModel(it->second, pos, 1.0f, WHITE);
                it++;
            }
        }
        EndMode3D();

        rlImGuiBegin();
        ImGui::Begin("Assets");
        int index = 0;
        for (const auto& [icon_name, texture] : icons) {
            rlImGuiImageButtonSize(icon_name.c_str(), &texture, Vector2(64, 64));
            if ((index & 1) == 0) {
                ImGui::SameLine();
            }
            index++;
        }
        ImGui::End();
        rlImGuiEnd();
        EndDrawing();
    }

    CloseWindow();
    rlImGuiShutdown();
    return 0;
}