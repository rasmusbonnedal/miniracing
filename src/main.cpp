#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "rlImGui.h"
#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <map>
#include <print>
#include <vector>

template<>
struct std::formatter<Vector3>
    : std::formatter<std::string>
{
    template<class FormatContext>
    typename FormatContext::iterator
        format(const Vector3& v, FormatContext& ctx) const
    {
        const std::string s = std::format("({}, {}, {})", v.x, v.y, v.z);
        return formatter<std::string>::format(s, ctx);
    }
};

BoundingBox merge_bb(const BoundingBox& bb1, const BoundingBox& bb2) {
    Vector3 min_corner = {
        std::min(bb1.min.x, bb2.min.x),
        std::min(bb1.min.y, bb2.min.y),
        std::min(bb1.min.z, bb2.min.z)
    };
    Vector3 max_corner = {
        std::max(bb1.max.x, bb2.max.x),
        std::max(bb1.max.y, bb2.max.y),
        std::max(bb1.max.z, bb2.max.z)
    };
    return { min_corner, max_corner };
}

Vector3 vec3(const float* fp) {
    return { fp[0], fp[1], fp[2] };
}

Vector3 get_vertex(const Mesh& mesh, int tri, int vertex) {
    if (mesh.indices) {
        return vec3(mesh.vertices + mesh.indices[tri * 3 + vertex] * 3);
    }
    else {
        return vec3(mesh.vertices + (tri * 3 + vertex) * 3);
    }
}


BoundingBox mesh_bounding_box(const Mesh& mesh) {
    BoundingBox bb = {};
    if (mesh.vertices && mesh.triangleCount > 0) {
        const Vector3 v = get_vertex(mesh, 0, 0);
        bb.min = bb.max = v;
        for (int t = 0; t < mesh.triangleCount; ++t) {
            for (int vertex = 0; vertex < 3; ++vertex) {
                const Vector3 v = get_vertex(mesh, t, vertex);
                bb.min = Vector3Min(bb.min, v);
                bb.max = Vector3Max(bb.max, v);
            }
        }
    }
    return bb;
}

struct RoadConnectorEdge {
    Vector3 start;
    Vector3 end;
    Vector3 outward;
};

// Mesh-local candidates on the four vertical AABB faces (Y is up).
// Weld positions because UV/normal seams and unindexed meshes duplicate vertices.
std::vector<RoadConnectorEdge> find_road_connector_edges(const Mesh& mesh) {
    std::vector<RoadConnectorEdge> connectors;
    if (!mesh.vertices || mesh.triangleCount <= 0) return connectors;
    const BoundingBox bb = mesh_bounding_box(mesh);
    const Vector3 extent = Vector3Subtract(bb.max, bb.min);
    const float epsilon = std::max(0.000001f,
        std::max({ extent.x, extent.y, extent.z }) * 0.00001f);
    std::vector<Vector3> positions;
    auto welded_index = [&](Vector3 p) {
        for (size_t i = 0; i < positions.size(); ++i) {
            if (Vector3DistanceSqr(positions[i], p) <= epsilon * epsilon) return i;
        }
        positions.push_back(p);
        return positions.size() - 1;
    };
    std::map<std::pair<size_t, size_t>, int> edge_counts;
    for (int t = 0; t < mesh.triangleCount; ++t) {
        const size_t ids[] = { welded_index(get_vertex(mesh, t, 0)),
            welded_index(get_vertex(mesh, t, 1)), welded_index(get_vertex(mesh, t, 2)) };
        if (ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0]) continue;
        for (int e = 0; e < 3; ++e) {
            const auto key = std::minmax(ids[e], ids[(e + 1) % 3]);
            ++edge_counts[{ key.first, key.second }];
        }
    }
    auto near = [epsilon](float a, float b) { return std::abs(a - b) <= epsilon; };
    for (const auto& [edge, count] : edge_counts) {
        if (count != 1) continue; // Exclude shared triangle edges and seams.
        const Vector3 a = positions[edge.first];
        const Vector3 b = positions[edge.second];
        // A connector needs horizontal width; vertical corner edges cannot align roads.
        if (std::hypot(b.x - a.x, b.z - a.z) <= epsilon) continue;
        if (extent.x > epsilon && near(a.x, bb.min.x) && near(b.x, bb.min.x))
            connectors.push_back({ a, b, { -1, 0, 0 } });
        else if (extent.x > epsilon && near(a.x, bb.max.x) && near(b.x, bb.max.x))
            connectors.push_back({ a, b, { 1, 0, 0 } });
        else if (extent.z > epsilon && near(a.z, bb.min.z) && near(b.z, bb.min.z))
            connectors.push_back({ a, b, { 0, 0, -1 } });
        else if (extent.z > epsilon && near(a.z, bb.max.z) && near(b.z, bb.max.z))
            connectors.push_back({ a, b, { 0, 0, 1 } });
    }
    return connectors;
}

BoundingBox model_bounding_box(const Model& model) {
    BoundingBox bb = {};
    if (model.meshCount > 0) {
        bb = mesh_bounding_box(model.meshes[0]);
        for (int i = 1; i < model.meshCount; ++i) {
            bb = merge_bb(bb, mesh_bounding_box(model.meshes[i]));
        }
    }
    return bb;
}

std::vector<RoadConnectorEdge> analyze_model(const Model& model) {
    std::vector<RoadConnectorEdge> road_connectors;
    int n_meshes = model.meshCount;
    std::print("Found model with {} meshes\n", n_meshes);
    BoundingBox model_bb = model_bounding_box(model);
    std::print("  BoundingBox: {} - {}\n", model_bb.min, model_bb.max);
    for (int i = 0; i < n_meshes; ++i) {
        const Mesh& mesh = model.meshes[i];
        int materialIndex = model.meshMaterial[i];
        const Material& material = model.materials[materialIndex];
        std::print("  Mesh {}:\n", i);
        std::print("    Vertices:  {}\n", mesh.vertexCount);
        std::print("    Triangles: {}\n", mesh.triangleCount);
        std::print("    Material:  {}\n", materialIndex);
        const MaterialMap& diffuse = material.maps[MATERIAL_MAP_ALBEDO];
        const Color& diffuse_col = diffuse.color;
        std::print("    Diffuse: {} {} {}\n", diffuse.color.r, diffuse.color.g, diffuse.color.b);
        if (diffuse_col.r == 68 && diffuse_col.g == 68 && diffuse_col.b == 68) {
            std::print("    FOUND THE ROAD MESH!\n");
            const auto connectors = find_road_connector_edges(mesh);
            road_connectors.insert(road_connectors.end(), connectors.begin(), connectors.end());
            std::print("    Road connector edge candidates: {}\n", connectors.size());
            for (const auto& edge : connectors) {
                const Vector3 midpoint = Vector3Scale(Vector3Add(edge.start, edge.end), 0.5f);
                std::print("      {} -> {}, midpoint: {}, width: {}, outward: {}\n",
                    edge.start, edge.end, midpoint, Vector3Distance(edge.start, edge.end),
                    edge.outward);
            }
        }
        BoundingBox bb = mesh_bounding_box(mesh);
        std::print("    BoundingBox: {} - {}\n", bb.min, bb.max);
    }
    return road_connectors;
}

Matrix object_transform(const Model& model, Vector3 position, float rotation_degrees) {
    // Match DrawModelEx, including the asset's own transform.
    const Matrix placement = MatrixMultiply(
        MatrixRotate(Vector3 { 0, 1, 0 }, rotation_degrees * DEG2RAD),
        MatrixTranslate(position.x, position.y, position.z));
    return MatrixMultiply(model.transform, placement);
}

RoadConnectorEdge transform_connector(const RoadConnectorEdge& edge, Matrix transform) {
    Matrix normal_transform = MatrixTranspose(MatrixInvert(transform));
    normal_transform.m12 = normal_transform.m13 = normal_transform.m14 = 0;
    return { Vector3Transform(edge.start, transform), Vector3Transform(edge.end, transform),
        Vector3Normalize(Vector3Transform(edge.outward, normal_transform)) };
}

void draw_road_connectors(const Model& model, Vector3 position, float rotation_degrees,
                          const std::vector<RoadConnectorEdge>& connectors, float alpha = 1.0f) {
    const Matrix transform = object_transform(model, position, rotation_degrees);
    constexpr float line_radius = 0.015f;
    for (const auto& edge : connectors) {
        const Vector3 start = Vector3Transform(edge.start, transform);
        const Vector3 end = Vector3Transform(edge.end, transform);
        if (Vector3DistanceSqr(start, end) > 0.00000001f) {
            // Cylinders give reliable thickness in 3D and remain visible on the surface.
            DrawCylinderEx(start, end, line_radius, line_radius, 8, Fade(RED, alpha));
        }
    }
}

struct ModelAsset {
    Model model;
    std::vector<RoadConnectorEdge> connectors;
    BoundingBox bounds;
};

Vector3 placement_anchor(const ModelAsset& asset, float rotation_degrees) {
    const Matrix transform = object_transform(asset.model, Vector3Zero(), rotation_degrees);
    BoundingBox bounds = {};
    for (int corner = 0; corner < 8; ++corner) {
        const Vector3 local = {
            (corner & 1) ? asset.bounds.max.x : asset.bounds.min.x,
            (corner & 2) ? asset.bounds.max.y : asset.bounds.min.y,
            (corner & 4) ? asset.bounds.max.z : asset.bounds.min.z
        };
        const Vector3 point = Vector3Transform(local, transform);
        if (corner == 0) bounds.min = bounds.max = point;
        else {
            bounds.min = Vector3Min(bounds.min, point);
            bounds.max = Vector3Max(bounds.max, point);
        }
    }
    // Y is up: center in X/Z, bottom in Y, including the model's transform.
    return { (bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y,
             (bounds.min.z + bounds.max.z) * 0.5f };
}

template<class K, class V>
const V* map_get(const std::map<K, V>& m, const K& key) {
    auto it = m.find(key);
    if (it != m.end()) {
        return &it->second;
    }
    return 0;
}

class Level {
public:
    static constexpr float snap_distance = 0.3f;
    static constexpr float connector_draw_distance = 6.0f;
    static constexpr float connector_full_alpha_distance = 3.0f;

    struct Object {
        const ModelAsset* asset;
        Vector3 position;
        float rotation_degrees;
    };

    const Object& object(int index) const { return objects.at(index); }

    void add_object(const ModelAsset& asset, Vector3 position, float rotation_degrees) {
        objects.push_back({ &asset, position, rotation_degrees });
    }

    void move_object(int index, Vector3 position, float rotation_degrees) {
        objects.at(index).position = position;
        objects.at(index).rotation_degrees = rotation_degrees;
    }

    int pick_object(Ray ray, Vector3* grab_point = nullptr) const {
        int picked = -1;
        float closest = std::numeric_limits<float>::max();
        for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
            const auto& obj = objects[i];
            const auto& model = obj.asset->model;
            const Matrix transform = object_transform(model, obj.position, obj.rotation_degrees);
            for (int m = 0; m < model.meshCount; ++m) {
                const RayCollision hit = GetRayCollisionMesh(ray, model.meshes[m], transform);
                if (hit.hit && hit.distance < closest) {
                    closest = hit.distance;
                    picked = i;
                    if (grab_point) *grab_point = hit.point;
                }
            }
        }
        return picked;
    }

    bool snap_position(const ModelAsset& asset, Vector3& position, float rotation_degrees,
                       int ignored_object = -1) const {
        float best_distance = snap_distance * snap_distance;
        Vector3 best_offset = {};
        bool found = false;
        const Matrix source_transform = object_transform(asset.model, position, rotation_degrees);
        for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
            if (i == ignored_object) continue;
            const auto& target = objects[i];
            const Matrix target_transform = object_transform(target.asset->model,
                target.position, target.rotation_degrees);
            for (const auto& local_source : asset.connectors) {
                const auto source = transform_connector(local_source, source_transform);
                const float width = Vector3Distance(source.start, source.end);
                if (width < 0.00001f) continue;
                for (const auto& local_target : target.asset->connectors) {
                    const auto edge = transform_connector(local_target, target_transform);
                    if (Vector3DotProduct(source.outward, edge.outward) > -0.99f) continue;
                    const Vector3 offset = Vector3Scale(Vector3Subtract(
                        Vector3Add(edge.start, edge.end),
                        Vector3Add(source.start, source.end)), 0.5f);
                    const float distance = Vector3LengthSqr(offset);
                    if (distance >= best_distance) continue;
                    // Both endpoints must meet: reject different widths and sloped/angled edges.
                    const Vector3 start = Vector3Add(source.start, offset);
                    const Vector3 end = Vector3Add(source.end, offset);
                    const float tolerance = std::max(0.001f, width * 0.01f);
                    const auto near = [tolerance](Vector3 a, Vector3 b) {
                        return Vector3DistanceSqr(a, b) <= tolerance * tolerance;
                    };
                    if (!((near(start, edge.start) && near(end, edge.end)) ||
                          (near(start, edge.end) && near(end, edge.start)))) continue;
                    best_distance = distance;
                    best_offset = offset;
                    found = true;
                }
            }
        }
        if (found) position = Vector3Add(position, best_offset);
        return found;
    }

    void draw(int hidden_object = -1, const Vector3* connector_focus = nullptr) const {
        for (const Object& object : objects) {
            if (hidden_object >= 0 && &object == &objects[hidden_object]) continue;
            DrawModelEx(object.asset->model, object.position, Vector3 { 0.0f, 1.0f, 0.0f },
                        object.rotation_degrees, Vector3 { 1.0f, 1.0f, 1.0f }, WHITE);
        }
        if (!connector_focus) return;
        for (const Object& object : objects) {
            if (hidden_object >= 0 && &object == &objects[hidden_object]) continue;
            const Vector3 center = Vector3Add(object.position,
                placement_anchor(*object.asset, object.rotation_degrees));
            const float distance = Vector3Distance(center, *connector_focus);
            if (distance >= connector_draw_distance) continue;
            const float alpha = std::clamp(
                (connector_draw_distance - distance) /
                (connector_draw_distance - connector_full_alpha_distance), 0.0f, 1.0f);
            draw_road_connectors(object.asset->model, object.position,
                object.rotation_degrees, object.asset->connectors, alpha);
        }
    }

private:
    std::vector<Object> objects;
};

#include "box3d/box3d.h"

struct physics_car {
	float spinSpeed = 30.0f;
	float maxSpinTorque = 5.0f;
	float suspensionHertz = 4.0f;
	float suspensionDampingRatio = 0.7f;
	float lowerTranslation = -0.2f;
	float upperTranslation = 0.2f;
	float steeringHertz = 10.0f;
	float steeringDampingRatio = 0.7f;
	float lowerSteeringDegrees = -45.0f;
	float upperSteeringDegrees = 45.0f;
	float maxSteeringTorque = 5.0f;
	float targetSteeringDegrees = 0.0f;
	b3BodyId chassis_id;
	b3JointId front_left_id;
	b3JointId front_right_id;
	b3JointId back_left_id;
	b3JointId back_right_id;

};
void create_test_physics_ground(const b3WorldId world_id, b3BodyId& ground_id, Model& ground_model);
void create_car_physics(physics_car& car, b3WorldId world_id);
void create_ground_model(const b3HeightFieldData* data, Model& ground_model);

int main() {
    const int screenWidth = 1920;
    const int screenHeight = 1080;

	int TARGET_FPS = 60;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Level Editor");
    SetExitKey(KEY_NULL);
    SetTargetFPS(TARGET_FPS);

    std::map<std::string, ModelAsset> models;
    std::map<std::string, Texture2D> icons;
    try {
        for (const auto& f : std::filesystem::directory_iterator(MODEL_PATH "Isometric")) {
            if (f.is_regular_file() && f.path().string().ends_with("NE.png")) {
                icons[f.path().stem().string()] = LoadTexture(f.path().string().c_str());
            }
        }
        for (const auto& f : std::filesystem::directory_iterator(MODEL_PATH "Models/GLTF Format")) {
            if (f.is_regular_file()) {
                auto& asset = models[f.path().stem().string()];
                asset.model = LoadModel(f.path().string().c_str());
                std::print("Analyzing {}\n", f.path().filename().string());
                asset.connectors = analyze_model(asset.model);
                asset.bounds = model_bounding_box(asset.model);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    Level level;
    const ModelAsset* selected_asset = nullptr;
    float placement_rotation_degrees = 0.0f;
    int moving_object = -1;
    Vector3 move_grab_offset = {};
    float move_plane_y = 0.0f;

    Camera3D camera = {0};
    camera.position = {44.0f, 44.0f, 44.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rlImGuiSetup(true);

	//Physics world setup
	b3WorldDef world_def = b3DefaultWorldDef();
	world_def.gravity = { 0.0f, -9.81f, 0.0f }; 
	b3WorldId world_id = b3CreateWorld(&world_def);
	float time_step = 1.0f / (float)(TARGET_FPS);
	int sub_step_count = 4;

	physics_car car;

	create_car_physics(car, world_id);
	b3BodyId ground_id;
	Model ground_model;
	create_test_physics_ground(world_id, ground_id, ground_model);


    while (!WindowShouldClose()) {
		//update physics
		b3Body_SetAwake(car.chassis_id, true);
		b3WheelJoint_SetSpinMotorSpeed(car.back_left_id, -car.spinSpeed * 1.0f);
		b3WheelJoint_SetSpinMotorSpeed(car.back_right_id, -car.spinSpeed * 1.0f);


		b3World_Step(world_id, time_step, sub_step_count);


        BeginDrawing();
        ClearBackground(RAYWHITE);

        rlImGuiBegin();

        const float mouse_wheel = GetMouseWheelMove();
        if (!ImGui::GetIO().WantCaptureMouse && mouse_wheel != 0.0f) {
            UpdateCameraPro(&camera, Vector3Zero(), Vector3Zero(), -mouse_wheel);
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            selected_asset = nullptr;
            moving_object = -1;
            placement_rotation_degrees = 0.0f;
        }

        Vector3 placement_position = {};
        bool has_placement_position = false;
        bool snapped = false;
        bool picked_up = false;
        const Ray mouse_ray = GetScreenToWorldRay(GetMousePosition(), camera);
        if (!selected_asset && !ImGui::GetIO().WantCaptureMouse &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && std::abs(mouse_ray.direction.y) > 0.0001f) {
            const float distance = -mouse_ray.position.y / mouse_ray.direction.y;
            if (distance >= 0.0f) {
                Vector3 grab_point = {};
                moving_object = level.pick_object(mouse_ray, &grab_point);
                if (moving_object >= 0) {
                    const auto& object = level.object(moving_object);
                    selected_asset = object.asset;
                    placement_rotation_degrees = object.rotation_degrees;
                    move_plane_y = grab_point.y;
                    // Store the picked point relative to the object before placement rotation.
                    move_grab_offset = Vector3Transform(Vector3Subtract(grab_point, object.position),
                        MatrixRotate(Vector3 { 0, 1, 0 }, -object.rotation_degrees * DEG2RAD));
                    picked_up = true;
                }
            }
        }
        if (selected_asset && !ImGui::GetIO().WantCaptureMouse) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                placement_rotation_degrees += 90.0f;
                if (placement_rotation_degrees >= 360.0f) {
                    placement_rotation_degrees = 0.0f;
                }
            }

            if (std::abs(mouse_ray.direction.y) > 0.0001f) {
                const float plane_y = moving_object >= 0 ? move_plane_y : 0.0f;
                const float ground_distance = (plane_y - mouse_ray.position.y) / mouse_ray.direction.y;
                const Vector3 ground_point = Vector3Add(
                    mouse_ray.position, Vector3Scale(mouse_ray.direction, ground_distance));
                if (ground_distance >= 0.0f) {
                    const Vector3 anchor = moving_object >= 0
                        ? Vector3Transform(move_grab_offset,
                            MatrixRotate(Vector3 { 0, 1, 0 }, placement_rotation_degrees * DEG2RAD))
                        : placement_anchor(*selected_asset, placement_rotation_degrees);
                    placement_position = Vector3Subtract(ground_point, anchor);
                    snapped = level.snap_position(*selected_asset, placement_position,
                        placement_rotation_degrees, moving_object);
                    has_placement_position = true;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !picked_up) {
                        if (moving_object >= 0) {
                            level.move_object(moving_object, placement_position, placement_rotation_degrees);
                            moving_object = -1;
                            selected_asset = nullptr;
                            has_placement_position = false;
                        } else {
                            level.add_object(*selected_asset, placement_position, placement_rotation_degrees);
                        }
                    }
                }
            }
        }

        BeginMode3D(camera);
        DrawGrid(10, 1.0f);
        const Vector3 connector_focus = has_placement_position
            ? Vector3Add(placement_position, placement_anchor(*selected_asset, placement_rotation_degrees))
            : Vector3Zero();
        level.draw(has_placement_position ? moving_object : -1,
            has_placement_position && !selected_asset->connectors.empty() ? &connector_focus : nullptr);
        if (has_placement_position) {
            DrawModelWiresEx(
                selected_asset->model, placement_position, Vector3 { 0.0f, 1.0f, 0.0f },
                placement_rotation_degrees, Vector3 { 1.0f, 1.0f, 1.0f }, snapped ? GREEN : BLUE);
            draw_road_connectors(selected_asset->model, placement_position,
                placement_rotation_degrees, selected_asset->connectors);
        }
		DrawModel(ground_model, { -20.0f, 0.0f, -20.0f }, 1.0f, RED);
		DrawModelWires(ground_model, { -20.0f, 0.0f, -20.0f }, 1.0f, DARKGREEN);

		b3Pos chassi_pos = b3Body_GetPosition(car.chassis_id);
		b3Quat chassi_rot = b3Body_GetRotation(car.chassis_id);
		float radians;
		b3Vec3 axis = b3GetAxisAngle(&radians, chassi_rot);

		rlPushMatrix();
		{
			rlTranslatef(chassi_pos.x, chassi_pos.y, chassi_pos.z);
			rlRotatef(radians * RAD2DEG, axis.x, axis.y, axis.z);
			DrawCube({ 0.0f, 0.0f, 0.0f }, 2.0f, 1.0f, 0.5f, YELLOW);
		}
		rlPopMatrix();

        EndMode3D();

        constexpr float asset_icon_size = 64.0f;
        constexpr float asset_column_count = 2.0f;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float asset_window_width =
            asset_column_count * (asset_icon_size + 2.0f * style.FramePadding.x) +
            (asset_column_count - 1.0f) * style.ItemSpacing.x +
            2.0f * style.WindowPadding.x + style.ScrollbarSize;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(asset_window_width, 0.0f),
            ImVec2(asset_window_width, std::numeric_limits<float>::max()));
        ImGui::Begin("Assets");
        int index = 0;
        for (const auto& [icon_name, texture] : icons) {
            const ModelAsset* asset = map_get(models, icon_name.substr(0, icon_name.length() - 3));
            const bool is_selected = asset && asset == selected_asset;
            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            const bool clicked = rlImGuiImageButtonSize(
                icon_name.c_str(), &texture, Vector2(asset_icon_size, asset_icon_size));
            if (is_selected) {
                ImGui::PopStyleColor();
            }
            if (clicked && asset) {
                moving_object = -1;
                selected_asset = asset;
                placement_rotation_degrees = 0.0f;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(
                    "%s",
                    icon_name.c_str());
            }
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
	b3DestroyWorld(world_id);
    return 0;
}



void create_car_physics(physics_car& car, b3WorldId world_id) {
	b3BodyDef bodyDef = b3DefaultBodyDef();
	b3ShapeDef shapeDef = b3DefaultShapeDef();


	

	{
		bodyDef.position = { 0.0f, 15.5f, 0.0f };
		bodyDef.type = b3_dynamicBody;
		car.chassis_id = b3CreateBody(world_id, &bodyDef);

		shapeDef.density = 0.5f;
		b3BoxHull box = b3MakeBoxHull(2.0f, 0.5f, 1.0f);
		b3CreateHullShape(car.chassis_id, &shapeDef, &box.base);
	}

	shapeDef.density = 2.0f;
	shapeDef.baseMaterial.friction = 3.0f;

	bodyDef.type = b3_dynamicBody;
	bodyDef.allowFastRotation = true;
	bodyDef.rotation = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisY, b3Vec3_axisZ);

	// b3HullData* hull = b3CreateCylinder( 0.25f, 0.4f, 0.0f, 16 );

	b3WheelJointDef jointDef = b3DefaultWheelJointDef();
	jointDef.base.bodyIdA = car.chassis_id;
	jointDef.base.localFrameA.q = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisX, b3Vec3_axisY);
	jointDef.base.localFrameB.q = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisZ, b3Vec3_axisY);
	jointDef.enableSuspensionLimit = true;
	jointDef.lowerSuspensionLimit = car.lowerTranslation;
	jointDef.upperSuspensionLimit = car.upperTranslation;
	jointDef.enableSuspensionSpring = true;
	jointDef.suspensionHertz = car.suspensionHertz;
	jointDef.suspensionDampingRatio = car.suspensionDampingRatio;
	jointDef.enableSpinMotor = true;
	jointDef.maxSpinTorque = car.maxSpinTorque;
	jointDef.enableSteering = true;
	jointDef.steeringHertz = car.steeringHertz;
	jointDef.steeringDampingRatio = car.steeringDampingRatio;
	jointDef.targetSteeringAngle = 0.0f;
	jointDef.maxSteeringTorque = car.maxSteeringTorque;
	jointDef.enableSteeringLimit = true;
	jointDef.lowerSteeringLimit = B3_PI / 180.0f * car.lowerSteeringDegrees;
	jointDef.upperSteeringLimit = B3_PI / 180.0f * car.upperSteeringDegrees;

	b3Sphere sphere = { b3Vec3_zero, 0.4f };

	{
		bodyDef.position = { 1.5f, 2.0f, 0.8f };
		b3BodyId bodyId = b3CreateBody(world_id, &bodyDef);
		b3CreateSphereShape(bodyId, &shapeDef, &sphere);
		// b3CreateHullShape( bodyId, &shapeDef, hull );

		jointDef.base.bodyIdB = bodyId;
		jointDef.base.localFrameA.p = { 1.5f, -0.5f, 0.8f };
		jointDef.enableSteering = true;
		jointDef.enableSpinMotor = false;
		car.front_left_id = b3CreateWheelJoint(world_id, &jointDef);
	}

	{
		bodyDef.position = { 1.5f, 2.0f, -0.8f };
		b3BodyId bodyId = b3CreateBody(world_id, &bodyDef);
		b3CreateSphereShape(bodyId, &shapeDef, &sphere);
		// b3CreateHullShape( bodyId, &shapeDef, hull );

		jointDef.base.bodyIdB = bodyId;
		jointDef.base.localFrameA.p = { 1.5f, -0.5f, -0.8f };
		jointDef.enableSteering = true;
		jointDef.enableSpinMotor = false;
		car.front_right_id = b3CreateWheelJoint(world_id, &jointDef);
	}

	{
		bodyDef.position = { -1.5f, 2.0f, 0.8f };
		b3BodyId bodyId = b3CreateBody(world_id, &bodyDef);
		b3CreateSphereShape(bodyId, &shapeDef, &sphere);
		// b3CreateHullShape( bodyId, &shapeDef, hull );

		jointDef.base.bodyIdB = bodyId;
		jointDef.base.localFrameA.p = { -1.5f, -0.5f, 0.8f };
		jointDef.enableSteering = false;
		jointDef.enableSpinMotor = true;
		car.back_left_id = b3CreateWheelJoint(world_id, &jointDef);
	}

	{
		bodyDef.position = { -1.5f, 2.0f, -0.8f };
		b3BodyId bodyId = b3CreateBody(world_id, &bodyDef);
		b3CreateSphereShape(bodyId, &shapeDef, &sphere);
		// b3CreateHullShape( bodyId, &shapeDef, hull );

		jointDef.base.bodyIdB = bodyId;
		jointDef.base.localFrameA.p = { -1.5f, -0.5f, -0.8f };
		jointDef.enableSteering = false;
		jointDef.enableSpinMotor = true;
		car.back_right_id = b3CreateWheelJoint(world_id, &jointDef);
	}

	return;

}

void create_test_physics_ground(const b3WorldId world_id, b3BodyId& ground_id, Model& ground_model) {

	b3BodyDef bodyDef = b3DefaultBodyDef();
	// bodyDef.position = { 0.0f, -1.0f, 0.0f };
	bodyDef.position = { -20.0f, 0.0f, -20.0f };
	ground_id = b3CreateBody(world_id, &bodyDef);

	b3ShapeDef shapeDef = b3DefaultShapeDef();
	// b3BoxHull groundBox = b3MakeBoxHull( 200.0f, 1.0f, 200.0f );
	// b3CreateHullShape( groundId, &shapeDef, &groundBox.base );

	b3HeightFieldData* heightField = b3CreateWave(50.0f, 50.0f, { 1.0f, 1.0f, 1.0f }, 0.1, 0.05f, false);
	// b3ShapeDef shapeDef = b3DefaultShapeDef();
	// b3SurfaceMaterial materials[3] = b3DefaultSurfaceMaterial();
	// materials[0] = { .friction = 0.6f, .restitution = 0.0f, 0 };
	// materials[1] = { .friction = 0.6f, .restitution = 1.0f, 1 };
	// materials[2] = { .friction = 0.6f, .restitution = 0.0f, 2 };
	// shapeDef.materials = materials;
	// shapeDef.materialCount = 3;

	b3CreateHeightFieldShape(ground_id, &shapeDef, heightField);
	
	create_ground_model(heightField, ground_model);
	
	return;

}

void create_ground_model(const b3HeightFieldData* data, Model &ground_model) {
	Mesh mesh = { 0 };

	int num_x = data->rowCount;
	int num_z = data->columnCount;

	mesh.vertexCount = num_x * num_z;
	mesh.triangleCount = (num_x - 1) * (num_z - 1) * 2;

	mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
	mesh.indices = (unsigned short*)RL_MALLOC(mesh.triangleCount * 3 * sizeof(unsigned short));

	const uint16_t* compressedHeights = b3GetHeightFieldCompressedHeights(data);

	float dx = data->scale.x;
	float dy = data->scale.y;
	float dz = data->scale.z;

	float y_range = data->maxHeight - data->minHeight;
	float y_scale = data->heightScale;

	int v_idx = 0;
	for (int z = 0; z < num_z; z++) {
		for (int x = 0; x < num_x; x++) {
			uint16_t val = compressedHeights[z * num_x + x];
			float y = ((float)val / 65535.0f);
			y = (data->minHeight + y * y_range)*dy;
			//printf("%f ", y);
			//y = sin((float)x/num_x*6.28) + cos((float)z/num_z*6.28)*dy;
			mesh.vertices[v_idx++] = (float)x * dx;
			mesh.vertices[v_idx++] = y;
			mesh.vertices[v_idx++] = (float)z * dz;

		}
	}

	int i_idx = 0;
	for (int z = 0; z < (num_z - 1); z++) {
		for (int x = 0; x < (num_x - 1); x++) {
			unsigned short current = z * num_x + x;
			unsigned short next = (z + 1) * num_x + x;
			
			mesh.indices[i_idx++] = current;
			mesh.indices[i_idx++] = next;
			mesh.indices[i_idx++] = current+1;

			mesh.indices[i_idx++] = next;
			mesh.indices[i_idx++] = next + 1;
			mesh.indices[i_idx++] = current + 1;
		}
	}
	UploadMesh(&mesh, false);
	ground_model = LoadModelFromMesh(mesh);
	return;
}
