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
    SetTargetFPS(TARGET_FPS);

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
        const float dt = GetFrameTime();
		//update physics
		b3World_Step(world_id, time_step, sub_step_count);

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
		DrawModel(ground_model, { 0.0f, 0.0f, 0.0f }, 1.0f, RED);
		DrawModelWires(ground_model, { 0.0f, 0.0f, 0.0f }, 1.0f, DARKGREEN);
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
	b3DestroyWorld(world_id);
    return 0;
}



void create_car_physics(physics_car& car, b3WorldId world_id) {
	b3BodyDef bodyDef = b3DefaultBodyDef();
	b3ShapeDef shapeDef = b3DefaultShapeDef();


	

	{
		bodyDef.position = { 0.0f, 2.5f, 0.0f };
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
