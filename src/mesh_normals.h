#pragma once

#include "raylib.h"
#include "raymath.h"
#include <algorithm>

// Generate area-weighted smooth normals before UploadMesh, preserving winding.
inline void generate_mesh_normals(Mesh& mesh) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return;
    if (!mesh.normals) mesh.normals = static_cast<float*>(MemAlloc(mesh.vertexCount * 3 * sizeof(float)));
    std::fill_n(mesh.normals, mesh.vertexCount * 3, 0.0f);
    for (int triangle = 0; triangle < mesh.triangleCount; ++triangle) {
        int ids[3];
        Vector3 p[3];
        for (int corner = 0; corner < 3; ++corner) {
            ids[corner] = mesh.indices ? mesh.indices[triangle * 3 + corner] : triangle * 3 + corner;
            const float* v = mesh.vertices + ids[corner] * 3;
            p[corner] = {v[0], v[1], v[2]};
        }
        const Vector3 normal = Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0]));
        for (int id : ids) {
            mesh.normals[id * 3] += normal.x;
            mesh.normals[id * 3 + 1] += normal.y;
            mesh.normals[id * 3 + 2] += normal.z;
        }
    }
    for (int i = 0; i < mesh.vertexCount; ++i) {
        float* n = mesh.normals + i * 3;
        Vector3 normal = {n[0], n[1], n[2]};
        normal = Vector3LengthSqr(normal) > 0.0f ? Vector3Normalize(normal) : Vector3 {0, 1, 0};
        n[0] = normal.x; n[1] = normal.y; n[2] = normal.z;
    }
}
