add_executable(ibl_test ibl_test.cpp ../src/ibl.cpp)
target_compile_features(ibl_test PRIVATE cxx_std_23)
target_include_directories(ibl_test PRIVATE ../src)
target_link_libraries(ibl_test PRIVATE raylib)
add_test(NAME diffuse_ibl COMMAND ibl_test
    "${CMAKE_SOURCE_DIR}/data/IBL/brown_photostudio_02_1k.hdr")

add_executable(ibl_shader_test ibl_shader_test.cpp ../src/ibl.cpp)
target_compile_features(ibl_shader_test PRIVATE cxx_std_23)
target_include_directories(ibl_shader_test PRIVATE ../src)
target_link_libraries(ibl_shader_test PRIVATE raylib)
add_test(NAME diffuse_ibl_shader COMMAND ibl_shader_test
    "${CMAKE_SOURCE_DIR}/src/shaders/diffuse_ibl.vs"
    "${CMAKE_SOURCE_DIR}/src/shaders/diffuse_ibl.fs")

add_executable(specular_ibl_test specular_ibl_test.cpp ../src/specular_ibl.cpp ../src/ibl.cpp)
target_compile_features(specular_ibl_test PRIVATE cxx_std_23)
target_include_directories(specular_ibl_test PRIVATE ../src)
target_link_libraries(specular_ibl_test PRIVATE raylib)
add_test(NAME specular_ibl COMMAND specular_ibl_test
    "${CMAKE_SOURCE_DIR}/src/shaders/diffuse_ibl.vs"
    "${CMAKE_SOURCE_DIR}/src/shaders/diffuse_ibl.fs"
    "${CMAKE_SOURCE_DIR}/data/IBL/brown_photostudio_02_1k.hdr")
