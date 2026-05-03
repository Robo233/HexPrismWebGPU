#include "Camera.hpp"
#include "InputController.hpp"
#include "ProceduralTerrain.hpp"
#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: "
                  << SDL_GetError()
                  << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "WebGPU Hexagonal Prism Voxel Test",
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: "
                  << SDL_GetError()
                  << "\n";
        SDL_Quit();
        return 1;
    }

    {
        WebGpuRenderer renderer(window);

        if (!renderer.initialize()) {
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        Camera camera{};
        camera.position = glm::vec3(0.0f, 12.0f, 34.0f);
        camera.pitch = -24.0f;
        camera.roll = 180.0f;
        InputController input{};

        ProceduralTerrainSettings terrainSettings{};
        HexCell visibleCenter =
            renderCenterForPosition(camera.position, terrainSettings);
        uint64_t prismRevision = 1;
        TerrainBuildResult initialTerrain =
            buildProceduralTerrain(visibleCenter, terrainSettings);
        std::vector<Prism> prisms =
            std::move(initialTerrain.prisms);

        std::future<TerrainBuildResult> pendingTerrain;
        bool hasPendingTerrain = false;

        auto requestTerrainBuild = [&](
            HexCell center
        ) {
            pendingTerrain =
                std::async(
                    std::launch::async,
                    buildProceduralTerrain,
                    center,
                    terrainSettings
                );
            hasPendingTerrain = true;
        };

        bool running = true;

        auto previousTime =
            std::chrono::high_resolution_clock::now();

        while (running) {
            auto currentTime =
                std::chrono::high_resolution_clock::now();

            float deltaTime =
                std::chrono::duration<float>(
                    currentTime - previousTime
                ).count();

            previousTime = currentTime;

            deltaTime = std::min(deltaTime, 0.05f);

            input.update(
                window,
                camera,
                deltaTime,
                running
            );

            HexCell cameraCenter =
                renderCenterForPosition(
                    camera.position,
                    terrainSettings
                );

            if (
                hasPendingTerrain &&
                pendingTerrain.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready
            ) {
                TerrainBuildResult result = pendingTerrain.get();
                hasPendingTerrain = false;

                int resultDistance =
                    hexDistanceBetweenCells(result.center, cameraCenter);
                int visibleDistance =
                    hexDistanceBetweenCells(visibleCenter, cameraCenter);
                bool resultMovesTerrainForward =
                    resultDistance < visibleDistance ||
                    sameRenderCenter(result.center, cameraCenter);

                if (resultMovesTerrainForward) {
                    visibleCenter = result.center;
                    prisms = std::move(result.prisms);
                    ++prismRevision;
                }
            }

            if (
                !sameRenderCenter(cameraCenter, visibleCenter) &&
                !hasPendingTerrain
            ) {
                requestTerrainBuild(cameraCenter);
            }

            renderer.render(prisms, camera, prismRevision);
        }
    }

    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
