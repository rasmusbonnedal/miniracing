set(PROJECT_NAME box3d_test)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME} main.cpp ibl.cpp specular_ibl.cpp ssao.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        raylib
        rlImGui
        box3d
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        MODEL_PATH="${CMAKE_SOURCE_DIR}/data/kenney_racing-kit/"
        IBL_PATH="${CMAKE_SOURCE_DIR}/data/IBL/brown_photostudio_02_1k.hdr"
        SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
)
