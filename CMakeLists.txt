cmake_minimum_required(VERSION 3.16)

project(box3d_test CXX)

# Match Box3D's static MSVC runtime across the application and all dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(ext)
add_subdirectory(src)
