#include "ibl.h"
#include "mesh_normals.h"
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

int main(int argc, char** argv) {
    // Sloped plane y=2x+3z: every vertex, including boundary vertices, must
    // have normal normalize(-2,1,-3), independent of the diagonal split.
    float vertices[] = {0,0,0, 1,2,0, 0,3,1, 1,5,1};
    unsigned short indices[] = {0,2,1, 2,3,1};
    Mesh plane {};
    plane.vertices = vertices; plane.indices = indices;
    plane.vertexCount = 4; plane.triangleCount = 2;
    generate_mesh_normals(plane);
    const Vector3 expected_normal = Vector3Normalize({-2,1,-3});
    bool normals_ok = true;
    for (int i = 0; i < 4; ++i) {
        Vector3 actual = {plane.normals[i*3],plane.normals[i*3+1],plane.normals[i*3+2]};
        normals_ok &= Vector3Distance(actual, expected_normal) < 0.00001f;
    }
    MemFree(plane.normals);
    if (!normals_ok) { std::fprintf(stderr, "Terrain normal check failed\n"); return 1; }
    constexpr double pi = std::numbers::pi;
    constexpr int width = 512, height = 256;
    std::vector<float> pixels(width * height * 3);
    // Constant, first-band, and second-band environments in separate channels.
    for (int row = 0; row < height; ++row) {
        const double theta = pi * (row + 0.5) / height;
        for (int col = 0; col < width; ++col) {
            const double phi = 2 * pi * (col + 0.5) / width - pi;
            const double x = std::sin(theta) * std::cos(phi);
            const double y = std::cos(theta);
            const double z = std::sin(theta) * std::sin(phi);
            float* p = pixels.data() + (row * width + col) * 3;
            p[0] = 4.0f;
            p[1] = static_cast<float>(2 + .4*x + .3*y + .2*z);
            p[2] = static_cast<float>(2 + .2*x*y + .3*y*z + .1*(3*z*z-1) + .4*x*z + .2*(x*x-y*y));
        }
    }
    const auto ibl = integrate_diffuse_ibl(pixels.data(), width, height);
    const Vector3 directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0},
        {0,0,1}, {0,0,-1}, {1,2,3}, {-2,3,-1}};
    for (auto n : directions) {
        const double length = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
        const double x = n.x/length, y = n.y/length, z = n.z/length;
        const double basis[] = {.282094791773878, .48860251190292*y,
            .48860251190292*z, .48860251190292*x, 1.09254843059208*x*y,
            1.09254843059208*y*z, .31539156525252*(3*z*z-1),
            1.09254843059208*x*z, .54627421529604*(x*x-y*y)};
        double actual[3] {};
        for (int i = 0; i < 9; ++i) {
            actual[0] += basis[i] * ibl.coefficients[i].x;
            actual[1] += basis[i] * ibl.coefficients[i].y;
            actual[2] += basis[i] * ibl.coefficients[i].z;
        }
        const double expected[] = {4*pi, 2*pi + 2*pi/3*(.4*x+.3*y+.2*z),
            2*pi + pi/4*(.2*x*y+.3*y*z+.1*(3*z*z-1)+.4*x*z+.2*(x*x-y*y))};
        for (int c = 0; c < 3; ++c) {
            if (std::abs(actual[c] - expected[c]) > .001) {
                std::fprintf(stderr, "Irradiance mismatch: %f vs %f\n", actual[c], expected[c]);
                return 1;
            }
        }
    }
    DiffuseIbl loaded;
    if (argc != 2 || !load_diffuse_ibl(argv[1], loaded)) return 1;
    for (const auto& c : loaded.coefficients)
        if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return 1;
    if (loaded.coefficients[0].x <= 0 || loaded.coefficients[0].y <= 0 || loaded.coefficients[0].z <= 0) return 1;
    std::puts("Diffuse IBL analytic and HDR loading checks passed.");
    return 0;
}
