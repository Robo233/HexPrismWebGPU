#include "ProceduralTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

int hexDistanceFromOrigin(int q, int r) {
    int s = -q - r;

    return std::max(
        std::max(std::abs(q), std::abs(r)),
        std::abs(s)
    );
}

uint32_t hashCoordinates(int q, int r, int seed) {
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

float random01(int q, int r, int seed) {
    constexpr float max24BitValue =
        static_cast<float>(0x01000000u);

    return static_cast<float>(
        hashCoordinates(q, r, seed) & 0x00ffffffu
    ) / max24BitValue;
}

float smoothstep(float edge0, float edge1, float value) {
    float t =
        std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 mixColor(
    glm::vec3 a,
    glm::vec3 b,
    float t
) {
    t = std::clamp(t, 0.0f, 1.0f);

    return a * (1.0f - t) + b * t;
}

int wrappedRotationStep(int q, int r, int y, int seed) {
    int step = q * 2 + r * 3 + y + seed;
    step %= 6;

    if (step < 0) {
        step += 6;
    }

    return step;
}

float lerp(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
}

float valueNoise2D(float x, float y, int seed) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);

    float bottom =
        lerp(
            random01(x0, y0, seed),
            random01(x1, y0, seed),
            tx
        );
    float top =
        lerp(
            random01(x0, y1, seed),
            random01(x1, y1, seed),
            tx
        );

    return lerp(bottom, top, ty);
}

float fbmNoise(
    float x,
    float y,
    int seed,
    int octaves
) {
    float total = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float normalizer = 0.0f;

    for (int octave = 0; octave < octaves; ++octave) {
        total +=
            valueNoise2D(
                x * frequency,
                y * frequency,
                seed + octave * 997
            ) * amplitude;
        normalizer += amplitude;

        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return total / normalizer;
}

float ridgedNoise(float value) {
    return 1.0f - std::abs(value * 2.0f - 1.0f);
}

glm::vec2 axialToTerrainPoint(int q, int r) {
    return glm::vec2(
        static_cast<float>(q) + static_cast<float>(r) * 0.5f,
        static_cast<float>(r) * 0.8660254038f
    );
}

struct TerrainSample {
    int height = 0;
    float river = 0.0f;
    float riverBank = 0.0f;
    float sand = 0.0f;
    float moisture = 0.0f;
    float grassVariation = 0.0f;
};

TerrainSample sampleTerrain(
    int q,
    int r,
    const ProceduralTerrainSettings& settings
) {
    glm::vec2 p = axialToTerrainPoint(q, r);
    glm::vec2 warp(
        (
            fbmNoise(
                p.x * 0.011f,
                p.y * 0.011f,
                settings.seed + 101,
                4
            ) - 0.5f
        ) * 34.0f,
        (
            fbmNoise(
                p.x * 0.011f,
                p.y * 0.011f,
                settings.seed + 151,
                4
            ) - 0.5f
        ) * 34.0f
    );

    glm::vec2 w = p + warp;

    float broad =
        fbmNoise(
            w.x * 0.0045f,
            w.y * 0.0045f,
            settings.seed + 11,
            5
        );
    float rolling =
        fbmNoise(
            w.x * 0.018f,
            w.y * 0.018f,
            settings.seed + 43,
            5
        );
    float fine =
        fbmNoise(
            w.x * 0.065f,
            w.y * 0.065f,
            settings.seed + 97,
            3
        );
    float ridge =
        ridgedNoise(
            fbmNoise(
                w.x * 0.024f,
                w.y * 0.024f,
                settings.seed + 311,
                4
            )
        );

    float riverField =
        ridgedNoise(
            fbmNoise(
                (p.x + warp.x * 0.45f) * 0.013f,
                (p.y + warp.y * 0.45f) * 0.013f,
                settings.seed + 709,
                5
            )
        );
    float riverRegion =
        smoothstep(
            0.38f,
            0.74f,
            fbmNoise(
                w.x * 0.005f,
                w.y * 0.005f,
                settings.seed + 727,
                3
            )
        );
    float river =
        smoothstep(0.905f, 0.985f, riverField) * riverRegion;
    float riverBank =
        smoothstep(0.74f, 0.92f, riverField) * riverRegion;

    float heightRange =
        static_cast<float>(
            settings.maxTerrainHeight - settings.minTerrainHeight
        );
    float hillMask =
        smoothstep(
            0.24f,
            0.82f,
            fbmNoise(
                w.x * 0.0075f,
                w.y * 0.0075f,
                settings.seed + 271,
                4
            )
        );
    float rollingHills = smoothstep(0.22f, 0.90f, rolling);
    float ridgeHills = smoothstep(0.38f, 0.94f, ridge);
    float elevation01 =
        0.06f +
        broad * 0.22f +
        rollingHills * (0.34f + hillMask * 0.22f) +
        ridgeHills * 0.13f +
        (fine - 0.5f) * 0.08f;

    elevation01 =
        std::clamp(
            elevation01 -
                riverBank * 0.08f -
                river * 0.22f,
            0.0f,
            1.0f
        );

    float height =
        static_cast<float>(settings.minTerrainHeight) +
        elevation01 * heightRange;

    if (river > 0.45f) {
        height =
            std::min(
                height,
                static_cast<float>(settings.minTerrainHeight) +
                    heightRange * (0.10f - river * 0.035f)
            );
    }

    int roundedHeight =
        std::clamp(
            static_cast<int>(std::round(height)),
            settings.minTerrainHeight,
            settings.maxTerrainHeight
        );

    TerrainSample terrain{};
    terrain.height = roundedHeight;
    terrain.river = river;
    terrain.riverBank = std::max(riverBank, river);
    terrain.moisture =
        fbmNoise(
            (p.x - 23.0f) * 0.018f,
            (p.y + 41.0f) * 0.018f,
            settings.seed + 503,
            4
        );
    terrain.grassVariation =
        fbmNoise(
            (p.x + 17.0f) * 0.075f,
            (p.y - 29.0f) * 0.075f,
            settings.seed + 607,
            3
        );

    return terrain;
}

glm::vec3 topColorForTerrain(
    int q,
    int r,
    const TerrainSample& terrain,
    const ProceduralTerrainSettings& settings
) {
    float heightT =
        static_cast<float>(terrain.height - settings.minTerrainHeight) /
        static_cast<float>(
            settings.maxTerrainHeight - settings.minTerrainHeight
        );
    float localVariation =
        (
            terrain.grassVariation +
            random01(q, r, settings.seed + 887) * 0.35f
        ) / 1.35f;

    if (terrain.sand > 0.35f) {
        return mixColor(
            glm::vec3(0.67f, 0.61f, 0.39f),
            glm::vec3(0.86f, 0.80f, 0.55f),
            localVariation * 0.65f + terrain.sand * 0.35f
        );
    }

    glm::vec3 shadedGrass(0.11f, 0.34f, 0.16f);
    glm::vec3 meadowGrass(0.21f, 0.53f, 0.22f);
    glm::vec3 hillGrass(0.32f, 0.61f, 0.27f);
    glm::vec3 dampGrass(0.13f, 0.43f, 0.25f);

    glm::vec3 base =
        mixColor(shadedGrass, meadowGrass, localVariation);
    base =
        mixColor(
            base,
            dampGrass,
            smoothstep(0.58f, 0.88f, terrain.moisture)
        );

    return mixColor(
        base,
        hillGrass,
        smoothstep(0.35f, 1.0f, heightT) * 0.55f
    );
}

glm::vec3 colorForLayer(
    int q,
    int r,
    int layer,
    const TerrainSample& terrain,
    const ProceduralTerrainSettings& settings
) {
    if (layer == terrain.height - 1) {
        return topColorForTerrain(q, r, terrain, settings);
    }

    float layerT =
        static_cast<float>(layer) /
        static_cast<float>(settings.maxTerrainHeight);
    float strata =
        (wrappedRotationStep(q, r, layer, 0) % 3 == 0) ? 1.0f : 0.0f;

    glm::vec3 lowerRock(0.12f, 0.14f, 0.15f);
    glm::vec3 upperRock(0.24f, 0.29f, 0.22f);
    glm::vec3 stratumRock(0.20f, 0.25f, 0.20f);

    return mixColor(
        mixColor(lowerRock, upperRock, layerT),
        stratumRock,
        strata * 0.25f
    );
}

HexCell worldToNearestHexCell(
    glm::vec3 position,
    const HexGridMetrics& metrics = HexGridMetrics{}
) {
    float qFloat = position.x / metrics.horizontalSpacing();
    float rFloat =
        position.z / metrics.diagonalSpacing() - qFloat * 0.5f;
    float sFloat = -qFloat - rFloat;

    int q = static_cast<int>(std::round(qFloat));
    int r = static_cast<int>(std::round(rFloat));
    int s = static_cast<int>(std::round(sFloat));

    float qDiff = std::abs(static_cast<float>(q) - qFloat);
    float rDiff = std::abs(static_cast<float>(r) - rFloat);
    float sDiff = std::abs(static_cast<float>(s) - sFloat);

    if (qDiff > rDiff && qDiff > sDiff) {
        q = -r - s;
    } else if (rDiff > sDiff) {
        r = -q - s;
    }

    return HexCell{ q, r, 0 };
}

int roundToStride(int value, int stride) {
    if (stride <= 1) {
        return value;
    }

    if (value >= 0) {
        return ((value + stride / 2) / stride) * stride;
    }

    return -(((-value + stride / 2) / stride) * stride);
}

void createProceduralPrisms(
    HexCell center,
    const ProceduralTerrainSettings& settings,
    std::vector<Prism>& prisms
) {
    prisms.clear();

    int renderRadius = settings.renderRadius;
    int diameter = renderRadius * 2 + 1;
    std::vector<TerrainSample> terrainSamples(
        static_cast<std::size_t>(diameter * diameter),
        TerrainSample{}
    );

    auto terrainIndex = [renderRadius, diameter](int dq, int dr) {
        return static_cast<std::size_t>(
            (dq + renderRadius) * diameter +
            (dr + renderRadius)
        );
    };

    auto heightAt = [
        center,
        renderRadius,
        &terrainSamples,
        &terrainIndex
    ](
        int q,
        int r
    ) {
        int dq = q - center.q;
        int dr = r - center.r;

        if (
            dq < -renderRadius ||
            dq > renderRadius ||
            dr < -renderRadius ||
            dr > renderRadius ||
            hexDistanceFromOrigin(dq, dr) > renderRadius
        ) {
            return 0;
        }

        return terrainSamples[terrainIndex(dq, dr)].height;
    };

    auto terrainAt = [
        center,
        renderRadius,
        &terrainSamples,
        &terrainIndex
    ](
        int q,
        int r
    ) -> const TerrainSample* {
        int dq = q - center.q;
        int dr = r - center.r;

        if (
            dq < -renderRadius ||
            dq > renderRadius ||
            dr < -renderRadius ||
            dr > renderRadius ||
            hexDistanceFromOrigin(dq, dr) > renderRadius
        ) {
            return nullptr;
        }

        return &terrainSamples[terrainIndex(dq, dr)];
    };

    std::size_t visibleTerrainCellCount =
        1u +
        static_cast<std::size_t>(3 * renderRadius * (renderRadius + 1));
    std::size_t estimatedPrismCount =
        visibleTerrainCellCount *
        static_cast<std::size_t>(std::min(settings.maxTerrainHeight, 10));

    if (prisms.capacity() < estimatedPrismCount) {
        prisms.reserve(estimatedPrismCount);
    }

    constexpr int neighborQ[6] = { 1, 1, 0, -1, -1, 0 };
    constexpr int neighborR[6] = { 0, -1, -1, 0, 1, 1 };

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;

            terrainSamples[terrainIndex(dq, dr)] =
                sampleTerrain(q, r, settings);
        }
    }

    constexpr float riverBedThreshold = 0.45f;

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;
            TerrainSample& terrain =
                terrainSamples[terrainIndex(dq, dr)];

            if (terrain.river > riverBedThreshold) {
                terrain.sand = 1.0f;
                continue;
            }

            float strongestAdjacentWater = 0.0f;
            int closestBedHeight = settings.maxTerrainHeight;

            for (int i = 0; i < 6; ++i) {
                const TerrainSample* neighbor =
                    terrainAt(q + neighborQ[i], r + neighborR[i]);

                if (
                    neighbor == nullptr ||
                    neighbor->river <= riverBedThreshold
                ) {
                    continue;
                }

                strongestAdjacentWater =
                    std::max(strongestAdjacentWater, neighbor->river);
                closestBedHeight =
                    std::min(closestBedHeight, neighbor->height);
            }

            if (strongestAdjacentWater <= 0.0f) {
                continue;
            }

            int heightAboveBed = terrain.height - closestBedHeight;
            bool lowShore =
                heightAboveBed >= -1 &&
                heightAboveBed <= 1;
            bool occasionalUpperBeach =
                heightAboveBed == 2 &&
                terrain.riverBank > 0.55f &&
                random01(q, r, settings.seed + 1231) > 0.55f;

            if (lowShore || occasionalUpperBeach) {
                terrain.sand =
                    std::clamp(
                        0.45f +
                            strongestAdjacentWater * 0.35f +
                            terrain.riverBank * 0.20f,
                        0.0f,
                        1.0f
                    );
            }
        }
    }

    HexGridMetrics metrics{};
    float horizontalSpacing = metrics.horizontalSpacing();
    float diagonalSpacing = metrics.diagonalSpacing();
    float prismHeight = metrics.prismHeight;

    auto addPrism = [&](
        int q,
        int r,
        int layer,
        int rotationStep,
        glm::vec3 color
    ) {
        prisms.emplace_back(
            glm::vec3(
                horizontalSpacing * static_cast<float>(q),
                prismHeight * static_cast<float>(layer),
                diagonalSpacing *
                    (
                        static_cast<float>(r) +
                        static_cast<float>(q) * 0.5f
                    )
            ),
            rotationStep,
            color
        );
    };

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;
            const TerrainSample* terrain = terrainAt(q, r);
            int terrainHeight = terrain == nullptr ? 0 : terrain->height;

            if (terrainHeight == 0) {
                continue;
            }

            int minNeighborHeight = settings.maxTerrainHeight;

            for (int i = 0; i < 6; ++i) {
                minNeighborHeight =
                    std::min(
                        minNeighborHeight,
                        heightAt(q + neighborQ[i], r + neighborR[i])
                    );
            }

            auto addTerrainLayer = [&](int layer) {
                addPrism(
                    q,
                    r,
                    layer,
                    wrappedRotationStep(q, r, layer, settings.seed),
                    colorForLayer(
                        q,
                        r,
                        layer,
                        *terrain,
                        settings
                    )
                );
            };

            if (terrainHeight == 1) {
                addTerrainLayer(0);
            } else {
                addTerrainLayer(0);

                int firstExposedMiddleLayer =
                    std::max(1, minNeighborHeight);

                for (
                    int layer = firstExposedMiddleLayer;
                    layer < terrainHeight - 1;
                    ++layer
                ) {
                    addTerrainLayer(layer);
                }

                addTerrainLayer(terrainHeight - 1);
            }
        }
    }
}

} // namespace

HexCell renderCenterForPosition(
    glm::vec3 position,
    const ProceduralTerrainSettings& settings
) {
    HexCell cameraCell = worldToNearestHexCell(position);

    return HexCell{
        roundToStride(cameraCell.q, settings.rebuildStride),
        roundToStride(cameraCell.r, settings.rebuildStride),
        0
    };
}

bool sameRenderCenter(HexCell a, HexCell b) {
    return a.q == b.q && a.r == b.r;
}

int hexDistanceBetweenCells(HexCell a, HexCell b) {
    return hexDistanceFromOrigin(a.q - b.q, a.r - b.r);
}

TerrainBuildResult buildProceduralTerrain(
    HexCell center,
    ProceduralTerrainSettings settings
) {
    TerrainBuildResult result{ center, {} };
    createProceduralPrisms(center, settings, result.prisms);

    return result;
}
