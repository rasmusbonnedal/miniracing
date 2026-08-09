# Box 3D
add_subdirectory(box3d)

# Raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable raylib examples" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "Disable raylib games" FORCE)
add_subdirectory(raylib)

# Dear ImGui
add_library(imgui_base
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui_base PUBLIC imgui)
set_target_properties(imgui_base PROPERTIES FOLDER "ext")

# rlImGui
add_library(rlImGui
    rlImGui/rlImGui.cpp
    rlImGui/rlImGui.h
    rlImGui/rlImGuiColors.h
)

target_include_directories(rlImGui PUBLIC rlImGui)
target_link_libraries(rlImGui PUBLIC raylib imgui_base)
