#include "Camera.hpp"
#include "InputController.hpp"
#include "Prism.hpp"
#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "HexGrid.hpp"

static int hexDistanceFromOrigin(int q, int r) {
    int s = -q - r;

    return std::max(
        std::max(std::abs(q), std::abs(r)),
        std::abs(s)
    );
}

static void addHexDisc(
    std::vector<Prism>& prisms,
    int centerQ,
    int centerR,
    int y,
    int radius,
    glm::vec3 prismColor
) {
    for (int dq = -radius; dq <= radius; ++dq) {
        for (int dr = -radius; dr <= radius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) <= radius) {
                prisms.push_back(
                    makePrismAtHexCell(
                        centerQ + dq,
                        centerR + dr,
                        y,
                        (dq - dr + y) % 6,
                        prismColor
                    )
                );
            }
        }
    }
}

static std::vector<Prism> createPrismTree(glm::vec3 prismColor) {
    std::vector<Prism> prisms;

    addHexDisc(prisms, 0, 0, -1, 4, prismColor);

    for (int y = 0; y <= 7; ++y) {
        prisms.push_back(
            makePrismAtHexCell(0, 0, y, y % 6, prismColor)
        );
    }

    const int rootY = 0;
    prisms.push_back(makePrismAtHexCell(1, 0, rootY, 0, prismColor));
    prisms.push_back(makePrismAtHexCell(-1, 0, rootY, 1, prismColor));
    prisms.push_back(makePrismAtHexCell(0, 1, rootY, 2, prismColor));
    prisms.push_back(makePrismAtHexCell(0, -1, rootY, 3, prismColor));
    prisms.push_back(makePrismAtHexCell(1, -1, rootY, 4, prismColor));
    prisms.push_back(makePrismAtHexCell(-1, 1, rootY, 5, prismColor));

    addHexDisc(prisms, 0, 0, 6, 2, prismColor);
    addHexDisc(prisms, 0, 0, 7, 3, prismColor);
    addHexDisc(prisms, 0, 0, 8, 2, prismColor);
    addHexDisc(prisms, 0, 0, 9, 1, prismColor);

    prisms.push_back(makePrismAtHexCell(0, 0, 10, 0, prismColor));

    return prisms;
}

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
        camera.position = glm::vec3(0.0f, 2.8f, 8.0f);
        camera.pitch = -18.0f;
        InputController input{};

        const glm::vec3 prismColor = glm::vec3(0.9f, 0.1f, 1.0f);
        std::vector<Prism> prisms = createPrismTree(prismColor);

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

            renderer.render(prisms, camera);
        }
    }

    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
