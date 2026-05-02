#include "Camera.hpp"
#include "InputController.hpp"
#include "Prism.hpp"
#include "WebGpuRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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

struct ProceduralPrismSettings {
    int seed = 24173;
    int radius = 9;
    int minColumnHeight = 1;
    int maxColumnHeight = 8;
};

static uint32_t hashCoordinates(int q, int r, int seed) {
    uint32_t h =
        static_cast<uint32_t>(q) * 0x8da6b343u ^
        static_cast<uint32_t>(r) * 0xd8163841u ^
        static_cast<uint32_t>(seed) * 0xcb1ab31fu;

    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;

    return h;
}

static float random01(int q, int r, int seed) {
    constexpr float max24BitValue =
        static_cast<float>(0x01000000u);

    return static_cast<float>(
        hashCoordinates(q, r, seed) & 0x00ffffffu
    ) / max24BitValue;
}

static float smoothedHexNoise(int q, int r, int seed) {
    constexpr int neighborQ[6] = { 1, 1, 0, -1, -1, 0 };
    constexpr int neighborR[6] = { 0, -1, -1, 0, 1, 1 };

    float total = random01(q, r, seed) * 2.0f;
    float weight = 2.0f;

    for (int i = 0; i < 6; ++i) {
        total += random01(q + neighborQ[i], r + neighborR[i], seed);
        weight += 1.0f;
    }

    return total / weight;
}

static glm::vec3 mixColor(
    glm::vec3 a,
    glm::vec3 b,
    float t
) {
    t = std::clamp(t, 0.0f, 1.0f);

    return a * (1.0f - t) + b * t;
}

static int wrappedRotationStep(int q, int r, int y, int seed) {
    int step = q * 2 + r * 3 + y + seed;
    step %= 6;

    if (step < 0) {
        step += 6;
    }

    return step;
}

static int columnHeightAt(
    int q,
    int r,
    const ProceduralPrismSettings& settings
) {
    int distance = hexDistanceFromOrigin(q, r);
    float distanceT =
        static_cast<float>(distance) /
        static_cast<float>(settings.radius);

    float islandFalloff = 1.0f - std::clamp(distanceT, 0.0f, 1.0f);
    float broadNoise = smoothedHexNoise(q / 2, r / 2, settings.seed);
    float detailNoise = smoothedHexNoise(q, r, settings.seed + 97);

    float ridge =
        std::sin(
            static_cast<float>(q) * 0.71f +
            static_cast<float>(r) * 0.43f +
            static_cast<float>(settings.seed) * 0.003f
        ) * 0.5f + 0.5f;

    float height =
        static_cast<float>(settings.minColumnHeight) +
        islandFalloff * 4.8f +
        (broadNoise - 0.5f) * 3.0f +
        detailNoise * 1.6f +
        ridge * islandFalloff * 1.4f;

    int roundedHeight = static_cast<int>(std::round(height));

    return std::clamp(
        roundedHeight,
        settings.minColumnHeight,
        settings.maxColumnHeight
    );
}

static glm::vec3 colorForLayer(
    int y,
    int columnHeight,
    const ProceduralPrismSettings& settings
) {
    float heightT =
        static_cast<float>(columnHeight - settings.minColumnHeight) /
        static_cast<float>(
            settings.maxColumnHeight - settings.minColumnHeight
        );

    if (y == columnHeight - 1) {
        glm::vec3 lowTop(0.12f, 0.62f, 0.52f);
        glm::vec3 highTop(0.94f, 0.22f, 0.86f);

        return mixColor(lowTop, highTop, heightT);
    }

    float layerT =
        static_cast<float>(y) /
        static_cast<float>(settings.maxColumnHeight);

    glm::vec3 lowerRock(0.19f, 0.20f, 0.27f);
    glm::vec3 upperRock(0.42f, 0.31f, 0.58f);

    return mixColor(lowerRock, upperRock, layerT);
}

static std::vector<Prism> createProceduralPrisms(
    const ProceduralPrismSettings& settings = ProceduralPrismSettings{}
) {
    std::vector<Prism> prisms;
    int diameter = settings.radius * 2 + 1;

    prisms.reserve(
        static_cast<std::size_t>(
            diameter * diameter * settings.maxColumnHeight
        )
    );

    for (int q = -settings.radius; q <= settings.radius; ++q) {
        for (int r = -settings.radius; r <= settings.radius; ++r) {
            if (hexDistanceFromOrigin(q, r) > settings.radius) {
                continue;
            }

            int columnHeight = columnHeightAt(q, r, settings);

            for (int layer = 0; layer < columnHeight; ++layer) {
                prisms.push_back(
                    makePrismAtHexCell(
                        q,
                        r,
                        layer,
                        wrappedRotationStep(q, r, layer, settings.seed),
                        colorForLayer(layer, columnHeight, settings)
                    )
                );
            }

            float accentChance = random01(q, r, settings.seed + 211);

            if (columnHeight >= 5 && accentChance > 0.9f) {
                int accentHeight =
                    1 + static_cast<int>(
                        random01(q, r, settings.seed + 353) * 3.0f
                    );

                for (int accentLayer = 0;
                     accentLayer < accentHeight;
                     ++accentLayer) {
                    int cellY = columnHeight + accentLayer;
                    float accentT =
                        static_cast<float>(accentLayer) /
                        static_cast<float>(accentHeight);

                    prisms.push_back(
                        makePrismAtHexCell(
                            q,
                            r,
                            cellY,
                            wrappedRotationStep(
                                q,
                                r,
                                cellY,
                                settings.seed + 5
                            ),
                            mixColor(
                                glm::vec3(0.18f, 0.78f, 0.94f),
                                glm::vec3(1.0f, 0.95f, 0.32f),
                                accentT
                            )
                        )
                    );
                }
            }
        }
    }

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
        camera.position = glm::vec3(0.0f, 6.0f, 11.0f);
        camera.pitch = -28.0f;
        camera.roll = 180.0f;
        InputController input{};

        std::vector<Prism> prisms = createProceduralPrisms();

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
