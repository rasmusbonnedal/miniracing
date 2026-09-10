#include "specular_ibl.h"
#include "ibl.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 4) return 1;
    constexpr int w=32,h=16;
    std::vector<float> pixels(w*h*3,4.0f);
    const auto cdf=environment_cdf(pixels.data(),w,h);
    if (cdf.empty() || cdf.back()!=1 || !std::is_sorted(cdf.begin(),cdf.end())) return 1;
    // Uniform radiance: probability tracks spherical area, not image area.
    const double pole=cdf[0], equator=cdf[8*w]-cdf[8*w-1];
    if (equator < 5*pole) return 1;
    pixels[(8*w+16)*3]=pixels[(8*w+16)*3+1]=pixels[(8*w+16)*3+2]=4000;
    const auto hot=environment_cdf(pixels.data(),w,h);
    if (hot[8*w+16]-hot[8*w+15] < .5) return 1;
    std::fill(pixels.begin(),pixels.end(),4.0f);
    const auto uniform=prefilter_specular_ibl(pixels.data(),w,h,16,128);
    for (float value : uniform.atlas) if (!std::isfinite(value) || std::abs(value-4)>0.00001f) return 1;
    for (size_t i=0;i<uniform.brdf.size();i+=3)
        if (!std::isfinite(uniform.brdf[i]) || uniform.brdf[i]<0 || uniform.brdf[i+1]<0 ||
            uniform.brdf[i]+uniform.brdf[i+1]>1.05f) return 1;
    std::fill(pixels.begin(),pixels.end(),0.0f);
    const auto black=prefilter_specular_ibl(pixels.data(),w,h,8,32);
    for (float value : black.atlas) if (value!=0) return 1;
    // A localized bright patch must spread and lose peak intensity as roughness increases.
    for (int y=6;y<10;++y) for (int x=14;x<18;++x)
        for (int c=0;c<3;++c) pixels[(y*w+x)*3+c]=10;
    const auto patch=prefilter_specular_ibl(pixels.data(),w,h,32,256);
    const int slice=(patch.width+2)*(patch.height+2)*3;
    const float sharp=*std::max_element(patch.atlas.begin(),patch.atlas.begin()+slice);
    const float rough=*std::max_element(patch.atlas.end()-slice,patch.atlas.end());
    if (!(sharp>rough*2 && rough>0)) return 1;

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(900,400,"Specular IBL test");
    DiffuseIbl diffuse;
    if (!load_diffuse_ibl(argv[3],diffuse)) return 1;
    Shader shader=load_diffuse_ibl_shader(argv[1],argv[2],diffuse);
    if (!shader.id) return 1;
    SpecularIbl specular;
    const auto start=std::chrono::steady_clock::now();
    if (!load_specular_ibl(argv[3],shader,specular)) return 1;
    std::printf("Specular preprocessing: %.2f seconds\n",
        std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    Model sphere=LoadModelFromMesh(GenMeshSphere(1,48,64));
    sphere.materials[0].shader=shader;
    attach_specular_ibl(sphere,specular);
    Camera camera={{0,1,9},{0,0,0},{0,1,0},35,CAMERA_PERSPECTIVE};
    SetShaderValue(shader,GetShaderLocation(shader,"cameraPosition"),&camera.position,SHADER_UNIFORM_VEC3);
    RenderTexture2D target=LoadRenderTexture(900,400);
    BeginTextureMode(target);
    ClearBackground({30,32,36,255});
    BeginMode3D(camera);
    sphere.materials[0].maps[MATERIAL_MAP_METALNESS].value=1;
    sphere.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value=0.1f;
    draw_ibl_model(sphere,{-2.5f,0,0},0,{1,1,1},{255,190,80,255});
    sphere.materials[0].maps[MATERIAL_MAP_METALNESS].value=0;
    sphere.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value=0.5f;
    draw_ibl_model(sphere,{0,0,0},0,{1,1,1},{120,160,255,255});
    sphere.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value=1;
    draw_ibl_model(sphere,{2.5f,0,0},0,{1,1,1},{120,160,255,255});
    EndMode3D();
    EndTextureMode();
    Image preview=LoadImageFromTexture(target.texture);
    ImageFlipVertical(&preview);
    ExportImage(preview,"specular_preview.png");
    UnloadImage(preview);
    // Constant prefiltered radiance and identity BRDF isolate metalness/F0.
    // Pure red metal should reflect red only; black dielectric still reflects 4% white.
    float white[3]={1,1,1};
    float identity[3]={1,0,0};
    Texture2D constant=LoadTextureFromImage({white,1,1,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32});
    Texture2D lut=LoadTextureFromImage({identity,1,1,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32});
    Model cube=LoadModelFromMesh(GenMeshCube(2,2,2));
    cube.materials[0].shader=shader;
    attach_specular_ibl(cube,{constant,lut});
    cube.materials[0].maps[MATERIAL_MAP_ROUGHNESS].value=.5f;
    bool passed=true;
    // Exercise enabled -> disabled -> enabled using the same cached textures.
    for (int test=0;test<4;++test) {
        const int metal=test==1 ? 1 : 0;
        const int enabled=test==2 ? 0 : 1;
        SetShaderValue(shader,GetShaderLocation(shader,"specularEnabled"),&enabled,SHADER_UNIFORM_INT);
        cube.materials[0].maps[MATERIAL_MAP_METALNESS].value=float(metal);
        BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginMode3D(camera);
        draw_ibl_model(cube,{0,0,0},0,{1,1,1},metal ? Color {255,0,0,255} : BLACK);
        EndMode3D();EndTextureMode();
        Image image=LoadImageFromTexture(target.texture);
        const Color c=GetImageColor(image,450,200);
        if (!enabled) passed &= c.r<=2 && c.g<=2 && c.b<=2;
        else if (metal) passed &= std::abs(int(c.r)-188)<=2 && c.g<=2 && c.b<=2;
        else passed &= std::abs(int(c.r)-55)<=2 && std::abs(int(c.g)-55)<=2 && std::abs(int(c.b)-55)<=2;
        std::printf("Enabled=%d Metal=%d pixel=%d,%d,%d\n",enabled,metal,c.r,c.g,c.b);
        UnloadImage(image);
    }
    UnloadTexture(constant);UnloadTexture(lut);UnloadModel(cube);
    UnloadModel(sphere);UnloadRenderTexture(target);
    UnloadTexture(specular.atlas);UnloadTexture(specular.brdf);UnloadShader(shader);
    CloseWindow();
    std::puts(passed ? "Specular IBL tests passed" : "Specular IBL tests FAILED");
    return passed ? 0 : 1;
}
