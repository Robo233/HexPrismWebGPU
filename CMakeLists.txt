cmake_minimum_required(VERSION 3.20)

project(HexPrismWebGPU LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Dawn CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(HexPrismWebGPU
    src/main.cpp
    src/Camera.cpp
    src/InputController.cpp
    src/HexGrid.cpp
    src/HexPrismMesh.cpp
    src/ProceduralTerrain.cpp
    src/WebGpuRenderer.cpp
)

target_link_libraries(HexPrismWebGPU
    PRIVATE
        dawn::webgpu_dawn
        SDL3::SDL3
        glm::glm
)

target_compile_definitions(HexPrismWebGPU
    PRIVATE
        NOMINMAX
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)
