#include "ProceduralTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

int hexDistanceFromOrigin(int q, int r) {
    int s = -q - r;

    return std::max(
        std::max(std::abs(q), std::abs(r)),
        std::abs(s)
    );
}

int fastFloor(float value) {
    int truncated = static_cast<int>(value);

    return truncated - static_cast<int>(value < static_cast<float>(truncated));
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
    int x0 = fastFloor(x);
    int y0 = fastFloor(y);
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

float normalizedTerrainHeight(
    int height,
    const ProceduralTerrainSettings& settings
) {
    float heightRange =
        static_cast<float>(
            settings.maxTerrainHeight - settings.minTerrainHeight
        );

    return static_cast<float>(height - settings.minTerrainHeight) /
        std::max(1.0f, heightRange);
}

float rockStrengthForHeight(float heightT) {
    return smoothstep(0.78f, 0.91f, heightT);
}

struct TerrainSample {
    int height = 0;
    float river = 0.0f;
    float riverBank = 0.0f;
    float sea = 0.0f;
    float seaBank = 0.0f;
    float lake = 0.0f;
    float lakeBank = 0.0f;
    float water = 0.0f;
    float waterBank = 0.0f;
    float sand = 0.0f;
    float moisture = 0.0f;
    float grassVariation = 0.0f;
    float rock = 0.0f;
};

struct HexOffset {
    int dq = 0;
    int dr = 0;
};

std::size_t hexCellCountForRadius(int radius) {
    return
        1u +
        static_cast<std::size_t>(3 * radius * (radius + 1));
}

const std::vector<HexOffset>& hexOffsetsWithinRadius(int radius) {
    static std::unordered_map<int, std::vector<HexOffset>> offsetsByRadius;

    auto cached = offsetsByRadius.find(radius);

    if (cached != offsetsByRadius.end()) {
        return cached->second;
    }

    std::vector<HexOffset> offsets;
    offsets.reserve(hexCellCountForRadius(radius));

    for (int dq = -radius; dq <= radius; ++dq) {
        for (int dr = -radius; dr <= radius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) <= radius) {
                offsets.push_back(HexOffset{ dq, dr });
            }
        }
    }

    auto inserted =
        offsetsByRadius.emplace(radius, std::move(offsets));

    return inserted.first->second;
}

uint64_t coordinateKey(int q, int r) {
    return
        (static_cast<uint64_t>(static_cast<uint32_t>(q)) << 32u) |
        static_cast<uint32_t>(r);
}

struct CachedTerrainSample {
    int q = 0;
    int r = 0;
    TerrainSample terrain;
};

struct TerrainSampleCache {
    int seed = 0;
    int minTerrainHeight = 0;
    int maxTerrainHeight = 0;
    bool initialized = false;
    std::unordered_map<uint64_t, CachedTerrainSample> samples;
};

TerrainSampleCache& terrainSampleCache() {
    static TerrainSampleCache cache;

    return cache;
}

void resetTerrainSampleCacheIfNeeded(
    TerrainSampleCache& cache,
    const ProceduralTerrainSettings& settings
) {
    if (
        cache.initialized &&
        cache.seed == settings.seed &&
        cache.minTerrainHeight == settings.minTerrainHeight &&
        cache.maxTerrainHeight == settings.maxTerrainHeight
    ) {
        return;
    }

    cache.seed = settings.seed;
    cache.minTerrainHeight = settings.minTerrainHeight;
    cache.maxTerrainHeight = settings.maxTerrainHeight;
    cache.initialized = true;
    cache.samples.clear();
}

void pruneTerrainSampleCache(
    TerrainSampleCache& cache,
    HexCell center,
    int retentionRadius,
    std::size_t pruneThreshold
) {
    if (cache.samples.size() <= pruneThreshold) {
        return;
    }

    for (auto it = cache.samples.begin(); it != cache.samples.end();) {
        const CachedTerrainSample& entry = it->second;

        if (
            hexDistanceFromOrigin(
                entry.q - center.q,
                entry.r - center.r
            ) > retentionRadius
        ) {
            it = cache.samples.erase(it);
        } else {
            ++it;
        }
    }
}

template <typename Func>
void parallelFor(std::size_t count, Func&& func) {
    constexpr std::size_t minParallelItems = 2048;

    if (count < minParallelItems) {
        func(0, count);
        return;
    }

    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    unsigned int workerCount =
        hardwareThreads > 1u ? hardwareThreads - 1u : 1u;
    workerCount = std::clamp(workerCount, 1u, 8u);
    workerCount =
        std::min<unsigned int>(
            workerCount,
            static_cast<unsigned int>(count)
        );

    if (workerCount <= 1u) {
        func(0, count);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    std::size_t chunkSize =
        (count + static_cast<std::size_t>(workerCount) - 1u) /
        static_cast<std::size_t>(workerCount);

    for (unsigned int worker = 0; worker < workerCount; ++worker) {
        std::size_t begin = static_cast<std::size_t>(worker) * chunkSize;
        std::size_t end = std::min(count, begin + chunkSize);

        if (begin >= end) {
            break;
        }

        workers.emplace_back([&, begin, end]() {
            func(begin, end);
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }
}

TerrainSample sampleTerrain(
    int q,
    int r,
    const ProceduralTerrainSettings& settings
) {
    glm::vec2 p = axialToTerrainPoint(q, r);
    glm::vec2 warp(
        (
            fbmNoise(
                p.x * 0.0095f,
                p.y * 0.0095f,
                settings.seed + 101,
                4
            ) - 0.5f
        ) * 40.0f,
        (
            fbmNoise(
                p.x * 0.0095f,
                p.y * 0.0095f,
                settings.seed + 151,
                4
            ) - 0.5f
        ) * 40.0f
    );

    glm::vec2 w = p + warp;

    float broad =
        fbmNoise(
            w.x * 0.0038f,
            w.y * 0.0038f,
            settings.seed + 11,
            5
        );
    float rolling =
        fbmNoise(
            w.x * 0.0145f,
            w.y * 0.0145f,
            settings.seed + 43,
            5
        );
    float fine =
        fbmNoise(
            w.x * 0.052f,
            w.y * 0.052f,
            settings.seed + 97,
            3
        );
    float ridge =
        ridgedNoise(
            fbmNoise(
                w.x * 0.0062f,
                w.y * 0.0062f,
                settings.seed + 311,
                4
            )
        );

    float riverField =
        ridgedNoise(
            fbmNoise(
                (p.x + warp.x * 0.42f) * 0.0110f,
                (p.y + warp.y * 0.42f) * 0.0110f,
                settings.seed + 709,
                5
            )
        );
    float riverRegion =
        smoothstep(
            0.38f,
            0.74f,
            fbmNoise(
                w.x * 0.0045f,
                w.y * 0.0045f,
                settings.seed + 727,
                3
            )
        );
    float river =
        smoothstep(0.905f, 0.985f, riverField) * riverRegion;
    float riverBank =
        smoothstep(0.74f, 0.92f, riverField) * riverRegion;

    float seaShape =
        fbmNoise(
            (w.x + warp.x * 0.18f) * 0.0023f,
            (w.y + warp.y * 0.18f) * 0.0023f,
            settings.seed + 8081,
            5
        );
    float seaCoastDetail =
        fbmNoise(
            w.x * 0.0085f,
            w.y * 0.0085f,
            settings.seed + 8093,
            3
        );
    float seaField = seaShape * 0.89f + seaCoastDetail * 0.11f;
    float sea = smoothstep(0.565f, 0.735f, seaField);
    float seaBank = smoothstep(0.455f, 0.635f, seaField);

    float lakeRegion =
        smoothstep(
            0.50f,
            0.82f,
            fbmNoise(
                (w.x - 91.0f) * 0.0052f,
                (w.y + 37.0f) * 0.0052f,
                settings.seed + 8123,
                4
            )
        );
    float lakeShape =
        fbmNoise(
            (w.x + 19.0f) * 0.0145f,
            (w.y - 53.0f) * 0.0145f,
            settings.seed + 8153,
            4
        );
    float largeLake =
        smoothstep(0.710f, 0.875f, lakeShape) * lakeRegion;
    float lakeBank =
        smoothstep(0.550f, 0.745f, lakeShape) * lakeRegion;

    float water = std::max(river, std::max(sea, largeLake));
    float waterBank =
        std::max(riverBank, std::max(seaBank, lakeBank));

    float heightRange =
        static_cast<float>(
            settings.maxTerrainHeight - settings.minTerrainHeight
        );
    float hillMask =
        smoothstep(
            0.24f,
            0.82f,
            fbmNoise(
                w.x * 0.0062f,
                w.y * 0.0062f,
                settings.seed + 271,
                4
            )
        );
    float continent = smoothstep(0.28f, 0.84f, broad);
    float rollingHills =
        smoothstep(0.50f, 0.90f, rolling) *
        smoothstep(0.24f, 0.84f, broad);
    float ridgeHills = smoothstep(0.58f, 0.94f, ridge);
    float mountainRegion =
        smoothstep(0.50f, 0.88f, broad) *
        smoothstep(0.48f, 0.86f, hillMask);
    float mountainLift =
        smoothstep(0.66f, 0.94f, ridge) *
        mountainRegion;
    float elevation01 =
        0.175f +
        continent * 0.125f +
        (rolling - 0.5f) * 0.075f +
        rollingHills * 0.105f +
        ridgeHills * mountainRegion * 0.055f +
        mountainLift * 0.390f +
        (fine - 0.5f) * 0.025f;

    float bankLowering =
        riverBank * 0.08f +
        lakeBank * 0.06f +
        seaBank * 0.015f;
    float bedLowering =
        river * 0.24f +
        largeLake * 0.42f +
        sea * 0.98f;

    elevation01 =
        std::clamp(
            elevation01 - bankLowering - bedLowering,
            0.0f,
            1.0f
        );

    float lowlandFlatness =
        1.0f - smoothstep(0.24f, 0.48f, elevation01);
    float lowlandStep = 2.0f / std::max(1.0f, heightRange);
    float terracedLowland =
        std::round(elevation01 / lowlandStep) * lowlandStep;

    elevation01 =
        lerp(
            elevation01,
            terracedLowland,
            lowlandFlatness * 0.55f
        );

    float height =
        static_cast<float>(settings.minTerrainHeight) +
        elevation01 * heightRange;

    if (sea > 0.34f) {
        float deepSea = smoothstep(0.34f, 0.78f, sea);
        float seaFloor01 = lerp(0.04f, -0.02f, deepSea);

        height =
            std::min(
                height,
                static_cast<float>(settings.minTerrainHeight) +
                    heightRange * seaFloor01
            );
    } else if (largeLake > 0.45f) {
        float deepLake = smoothstep(0.45f, 0.88f, largeLake);
        float lakeFloor01 = lerp(0.14f, 0.02f, deepLake);

        height =
            std::min(
                height,
                static_cast<float>(settings.minTerrainHeight) +
                    heightRange * lakeFloor01
            );
    } else if (river > 0.45f) {
        height =
            std::min(
                height,
                static_cast<float>(settings.minTerrainHeight) +
                    heightRange * (0.12f - river * 0.055f)
            );
    }

    int roundedHeight =
        std::clamp(
            static_cast<int>(std::round(height)),
            settings.minTerrainHeight,
            settings.maxTerrainHeight
        );

    float normalizedHeight = normalizedTerrainHeight(roundedHeight, settings);
    float lowCoastMask =
        1.0f - smoothstep(0.18f, 0.34f, normalizedHeight);

    float riverSand =
        river * 0.95f +
        riverBank * 0.30f;
    float seaBedSand =
        sea > 0.38f
            ? smoothstep(0.38f, 0.70f, sea) * 0.98f
            : 0.0f;
    float lakeBedSand =
        largeLake > 0.45f
            ? smoothstep(0.45f, 0.78f, largeLake) * 0.90f
            : 0.0f;
    float seaBeachHint =
        smoothstep(0.50f, 0.68f, seaBank) *
        lowCoastMask *
        (1.0f - smoothstep(0.30f, 0.62f, sea)) *
        0.46f;
    float lakeBeachHint =
        smoothstep(0.58f, 0.74f, lakeBank) *
        (1.0f - smoothstep(0.14f, 0.26f, normalizedHeight)) *
        (1.0f - smoothstep(0.34f, 0.70f, largeLake)) *
        0.18f;

    TerrainSample terrain{};
    terrain.height = roundedHeight;
    terrain.river = river;
    terrain.riverBank = std::max(riverBank, river);
    terrain.sea = sea;
    terrain.seaBank = std::max(seaBank, sea);
    terrain.lake = largeLake;
    terrain.lakeBank = std::max(lakeBank, largeLake);
    terrain.water = water;
    terrain.waterBank = std::max(waterBank, water);
    terrain.sand =
        std::clamp(
            riverSand +
                seaBedSand +
                lakeBedSand +
                seaBeachHint +
                lakeBeachHint,
            0.0f,
            1.0f
        );
    terrain.moisture =
        fbmNoise(
            (p.x - 23.0f) * 0.0140f,
            (p.y + 41.0f) * 0.0140f,
            settings.seed + 503,
            4
        );
    terrain.grassVariation =
        fbmNoise(
            (p.x + 17.0f) * 0.060f,
            (p.y - 29.0f) * 0.060f,
            settings.seed + 607,
            3
        );
    terrain.rock = rockStrengthForHeight(normalizedHeight);

    return terrain;
}

glm::vec3 topColorForTerrain(
    int q,
    int r,
    const TerrainSample& terrain,
    const ProceduralTerrainSettings& settings
) {
    float heightT = normalizedTerrainHeight(terrain.height, settings);
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
    glm::vec3 hillGrass(0.28f, 0.55f, 0.24f);
    glm::vec3 forestFloor(0.07f, 0.24f, 0.09f);
    glm::vec3 dampGrass(0.13f, 0.43f, 0.25f);

    float forestBand =
        smoothstep(0.18f, 0.54f, heightT) *
        (1.0f - smoothstep(0.64f, 0.76f, heightT));
    float grassAmount = 1.0f - terrain.rock;

    glm::vec3 base =
        mixColor(shadedGrass, meadowGrass, localVariation);
    base =
        mixColor(
            base,
            dampGrass,
            smoothstep(0.58f, 0.88f, terrain.moisture) * grassAmount
        );
    base =
        mixColor(
            base,
            hillGrass,
            smoothstep(0.30f, 0.62f, heightT) * 0.45f * grassAmount
        );
    base = mixColor(base, forestFloor, forestBand * 0.28f * grassAmount);

    float rockVariation =
        (
            random01(q, r, settings.seed + 1703) * 0.45f +
            fbmNoise(
                static_cast<float>(q) * 0.09f,
                static_cast<float>(r) * 0.09f,
                settings.seed + 1711,
                3
            ) * 0.55f
        );
    glm::vec3 darkRock(0.23f, 0.24f, 0.23f);
    glm::vec3 lightRock(0.47f, 0.48f, 0.45f);
    glm::vec3 mountainRock = mixColor(darkRock, lightRock, rockVariation);

    return mixColor(base, mountainRock, terrain.rock);
}

glm::vec3 dirtColorForLayer(
    int q,
    int r,
    int layer,
    const ProceduralTerrainSettings& settings
) {
    float layerVariation =
        random01(q, r, settings.seed + 1409) * 0.45f +
        random01(q, layer, settings.seed + 1423) * 0.35f +
        random01(r, layer, settings.seed + 1439) * 0.20f;
    float stratum =
        (wrappedRotationStep(q, r, layer, settings.seed + 1471) % 4 == 0)
            ? 1.0f
            : 0.0f;

    glm::vec3 darkDirt(0.20f, 0.13f, 0.07f);
    glm::vec3 midDirt(0.36f, 0.24f, 0.13f);
    glm::vec3 dryDirt(0.48f, 0.34f, 0.19f);

    glm::vec3 dirt = mixColor(darkDirt, midDirt, layerVariation);

    return mixColor(dirt, dryDirt, stratum * 0.18f);
}

glm::vec3 rockColorForLayer(
    int q,
    int r,
    int layer,
    const ProceduralTerrainSettings& settings
) {
    float layerT =
        static_cast<float>(layer) /
        static_cast<float>(settings.maxTerrainHeight);
    float strata =
        (wrappedRotationStep(q, r, layer, 0) % 3 == 0) ? 1.0f : 0.0f;

    glm::vec3 lowerRock(0.12f, 0.14f, 0.15f);
    glm::vec3 upperRock(0.24f, 0.29f, 0.22f);
    glm::vec3 stratumRock(0.20f, 0.25f, 0.20f);
    glm::vec3 mountainRock(0.36f, 0.37f, 0.35f);

    glm::vec3 rock =
        mixColor(
            mixColor(lowerRock, upperRock, layerT),
            stratumRock,
            strata * 0.25f
        );

    return mixColor(rock, mountainRock, smoothstep(0.70f, 0.95f, layerT));
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

    glm::vec3 dirt = dirtColorForLayer(q, r, layer, settings);
    glm::vec3 rock = rockColorForLayer(q, r, layer, settings);

    return mixColor(dirt, rock, terrain.rock);
}

float forestBiomeStrength(
    int q,
    int r,
    const ProceduralTerrainSettings& settings
) {
    glm::vec2 p = axialToTerrainPoint(q, r);

    float broadForest =
        fbmNoise(
            p.x * 0.0140f,
            p.y * 0.0140f,
            settings.seed + 2027,
            4
        );
    float forestEdgeVariation =
        fbmNoise(
            p.x * 0.040f,
            p.y * 0.040f,
            settings.seed + 2069,
            3
        );
    float groveVariation =
        fbmNoise(
            p.x * 0.085f,
            p.y * 0.085f,
            settings.seed + 2089,
            3
        );

    return smoothstep(
        0.38f,
        0.64f,
        broadForest * 0.78f +
            forestEdgeVariation * 0.16f +
            groveVariation * 0.06f
    );
}

bool shouldPlaceTree(
    int q,
    int r,
    const TerrainSample& terrain,
    const ProceduralTerrainSettings& settings
) {
    if (terrain.height <= settings.minTerrainHeight + 1) {
        return false;
    }

    if (
        terrain.water > 0.35f ||
        terrain.sand > 0.20f ||
        terrain.waterBank > 0.46f ||
        terrain.rock > 0.12f
    ) {
        return false;
    }

    float heightT = normalizedTerrainHeight(terrain.height, settings);

    float biomeStrength = forestBiomeStrength(q, r, settings);
    float smallPatchNoise =
        fbmNoise(
            static_cast<float>(q) * 0.130f,
            static_cast<float>(r) * 0.130f,
            settings.seed + 2141,
            4
        );

    float scatteredPatchMask = smoothstep(0.50f, 0.78f, smallPatchNoise);
    float moistureMask = smoothstep(0.30f, 0.76f, terrain.moisture);
    float altitudeBoost = smoothstep(0.07f, 0.48f, heightT);
    float highForestFade = 1.0f - smoothstep(0.60f, 0.74f, heightT);
    float beachTreePenalty = smoothstep(0.03f, 0.17f, heightT);
    float alpineLimit = altitudeBoost * highForestFade;
    float forestDensityBoost = 0.72f + altitudeBoost * 0.55f;

    float scatteredChance =
        scatteredPatchMask *
        moistureMask *
        alpineLimit *
        beachTreePenalty *
        forestDensityBoost *
        0.30f;
    float forestChance =
        biomeStrength *
        moistureMask *
        alpineLimit *
        beachTreePenalty *
        forestDensityBoost *
        1.08f;
    float placementChance = std::max(scatteredChance, forestChance);

    return
        placementChance > 0.018f &&
        random01(q, r, settings.seed + 3001) < placementChance;
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

int seaLevelLayerForSettings(
    const ProceduralTerrainSettings& settings
) {
    float heightRange =
        static_cast<float>(
            settings.maxTerrainHeight - settings.minTerrainHeight
        );

    return std::clamp(
        static_cast<int>(
            std::round(
                static_cast<float>(settings.minTerrainHeight) +
                    heightRange * 0.16f
            )
        ),
        settings.minTerrainHeight,
        settings.maxTerrainHeight
    );
}

glm::vec3 waterColorForTerrain(
    int q,
    int r,
    const TerrainSample& terrain,
    const ProceduralTerrainSettings& settings
) {
    float localVariation =
        fbmNoise(
            static_cast<float>(q) * 0.11f,
            static_cast<float>(r) * 0.11f,
            settings.seed + 9109,
            3
        );
    float depthHint =
        std::clamp(terrain.water * 0.82f + terrain.sea * 0.18f, 0.0f, 1.0f);

    glm::vec3 shallowWater(0.16f, 0.58f, 0.76f);
    glm::vec3 deepWater(0.02f, 0.20f, 0.48f);

    return mixColor(
        mixColor(shallowWater, deepWater, depthHint),
        glm::vec3(0.34f, 0.78f, 0.88f),
        localVariation * 0.12f
    );
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

    const std::vector<HexOffset>& visibleOffsets =
        hexOffsetsWithinRadius(renderRadius);
    std::size_t visibleTerrainCellCount = visibleOffsets.size();

    std::size_t estimatedPrismCount =
        visibleTerrainCellCount *
        static_cast<std::size_t>(std::min(settings.maxTerrainHeight, 18)) +
        visibleTerrainCellCount * 5u;

    if (prisms.capacity() < estimatedPrismCount) {
        prisms.reserve(estimatedPrismCount);
    }

    constexpr int neighborQ[6] = { 1, 1, 0, -1, -1, 0 };
    constexpr int neighborR[6] = { 0, -1, -1, 0, 1, 1 };

    TerrainSampleCache& cache = terrainSampleCache();
    resetTerrainSampleCacheIfNeeded(cache, settings);

    if (cache.samples.bucket_count() < visibleTerrainCellCount * 2u) {
        cache.samples.reserve(visibleTerrainCellCount * 2u);
    }

    std::vector<HexOffset> missingTerrainOffsets;
    missingTerrainOffsets.reserve(visibleTerrainCellCount / 8u);

    for (const HexOffset& offset : visibleOffsets) {
        int q = center.q + offset.dq;
        int r = center.r + offset.dr;
        auto cached = cache.samples.find(coordinateKey(q, r));

        if (cached != cache.samples.end()) {
            terrainSamples[terrainIndex(offset.dq, offset.dr)] =
                cached->second.terrain;
        } else {
            missingTerrainOffsets.push_back(offset);
        }
    }

    parallelFor(
        missingTerrainOffsets.size(),
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const HexOffset& offset = missingTerrainOffsets[i];
                int q = center.q + offset.dq;
                int r = center.r + offset.dr;

                terrainSamples[terrainIndex(offset.dq, offset.dr)] =
                    sampleTerrain(q, r, settings);
            }
        }
    );

    for (const HexOffset& offset : missingTerrainOffsets) {
        int q = center.q + offset.dq;
        int r = center.r + offset.dr;

        cache.samples.emplace(
            coordinateKey(q, r),
            CachedTerrainSample{
                q,
                r,
                terrainSamples[terrainIndex(offset.dq, offset.dr)]
            }
        );
    }

    int cacheRetentionRadius =
        renderRadius + std::max(settings.rebuildStride * 4, 64);
    pruneTerrainSampleCache(
        cache,
        center,
        cacheRetentionRadius,
        visibleTerrainCellCount * 3u
    );

    constexpr float waterBedThreshold = 0.45f;

    int seaLevelLayer = seaLevelLayerForSettings(settings);

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;
            TerrainSample& terrain =
                terrainSamples[terrainIndex(dq, dr)];

            float strongestAdjacentSea = terrain.sea;
            float strongestAdjacentInlandWater =
                std::max(terrain.river, terrain.lake);
            float strongestAdjacentAnyWater = terrain.water;
            int closestSeaBedHeight = terrain.height;
            int closestInlandBedHeight = terrain.height;

            for (int i = 0; i < 6; ++i) {
                const TerrainSample* neighbor =
                    terrainAt(q + neighborQ[i], r + neighborR[i]);

                if (neighbor == nullptr) {
                    continue;
                }

                strongestAdjacentAnyWater =
                    std::max(strongestAdjacentAnyWater, neighbor->water);

                if (neighbor->sea > waterBedThreshold) {
                    strongestAdjacentSea =
                        std::max(strongestAdjacentSea, neighbor->sea);
                    closestSeaBedHeight =
                        std::min(closestSeaBedHeight, neighbor->height);
                }

                float inlandWater = std::max(neighbor->river, neighbor->lake);

                if (inlandWater > waterBedThreshold) {
                    strongestAdjacentInlandWater =
                        std::max(strongestAdjacentInlandWater, inlandWater);
                    closestInlandBedHeight =
                        std::min(closestInlandBedHeight, neighbor->height);
                }
            }

            float normalizedHeight = normalizedTerrainHeight(terrain.height, settings);

            if (strongestAdjacentSea > 0.0f) {
                int heightAboveSeaBed = terrain.height - closestSeaBedHeight;
                bool lowCoastalBeach =
                    heightAboveSeaBed >= 0 &&
                    heightAboveSeaBed <= 3 &&
                    terrain.height <= seaLevelLayer + 3;
                bool slightlyRaisedCoast =
                    heightAboveSeaBed == 4 &&
                    terrain.height <= seaLevelLayer + 3 &&
                    terrain.seaBank > 0.58f &&
                    normalizedHeight < 0.26f;

                if (lowCoastalBeach || slightlyRaisedCoast) {
                    float beachStrength =
                        std::clamp(
                            0.58f +
                                strongestAdjacentSea * 0.28f +
                                terrain.seaBank * 0.22f -
                                normalizedHeight * 0.18f,
                            0.0f,
                            1.0f
                        );
                    terrain.sand = std::max(terrain.sand, beachStrength);
                }
            }

            if (strongestAdjacentInlandWater > 0.0f) {
                int heightAboveBed = terrain.height - closestInlandBedHeight;
                bool lowShore =
                    heightAboveBed >= -1 &&
                    heightAboveBed <= 1 &&
                    terrain.height <= seaLevelLayer + 2;
                bool occasionalUpperBeach =
                    heightAboveBed == 2 &&
                    terrain.height <= seaLevelLayer + 2 &&
                    terrain.waterBank > 0.55f &&
                    random01(q, r, settings.seed + 1231) > 0.55f;

                if (lowShore || occasionalUpperBeach) {
                    terrain.sand =
                        std::max(
                            terrain.sand,
                            std::clamp(
                                0.45f +
                                    strongestAdjacentInlandWater * 0.35f +
                                    terrain.waterBank * 0.20f,
                                0.0f,
                                1.0f
                            )
                        );
                }
            }

            bool atOrBelowSeaLevel = terrain.height <= seaLevelLayer;
            bool immediateCoast = terrain.height <= seaLevelLayer + 1;
            bool connectedToWater =
                strongestAdjacentAnyWater > 0.08f ||
                terrain.waterBank > 0.18f ||
                strongestAdjacentSea > 0.08f ||
                strongestAdjacentInlandWater > 0.12f;

            if (atOrBelowSeaLevel) {
                terrain.sand = 1.0f;

                if (connectedToWater) {
                    float shorelineWaterStrength =
                        std::clamp(
                            std::max(
                                std::max(terrain.water, strongestAdjacentAnyWater * 0.96f),
                                std::max(
                                    strongestAdjacentSea * 0.98f,
                                    strongestAdjacentInlandWater * 0.96f
                                )
                            ),
                            0.0f,
                            1.0f
                        );

                    terrain.water = std::max(
                        terrain.water,
                        std::max(shorelineWaterStrength, 0.55f)
                    );
                    terrain.waterBank = std::max(terrain.waterBank, terrain.water);

                    if (strongestAdjacentSea >= strongestAdjacentInlandWater) {
                        terrain.sea = std::max(
                            terrain.sea,
                            std::max(strongestAdjacentSea * 0.95f, 0.55f)
                        );
                        terrain.seaBank = std::max(terrain.seaBank, terrain.sea);
                    } else if (terrain.lake >= terrain.river) {
                        terrain.lake = std::max(
                            terrain.lake,
                            std::max(strongestAdjacentInlandWater * 0.93f, 0.55f)
                        );
                        terrain.lakeBank = std::max(terrain.lakeBank, terrain.lake);
                    } else {
                        terrain.river = std::max(
                            terrain.river,
                            std::max(strongestAdjacentInlandWater * 0.93f, 0.55f)
                        );
                        terrain.riverBank = std::max(terrain.riverBank, terrain.river);
                    }
                }
            } else if (immediateCoast && connectedToWater) {
                terrain.sand = std::max(terrain.sand, 1.0f);
            }
        }
    }

    constexpr int maxBeachDistanceFromWater = 2;
    constexpr int maxBeachHeightAboveSeaLevel = 3;
    constexpr float visibleWaterThreshold = 0.05f;

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;
            TerrainSample& terrain =
                terrainSamples[terrainIndex(dq, dr)];

            bool isWaterCell =
                terrain.height <= seaLevelLayer &&
                terrain.water > visibleWaterThreshold;

            if (isWaterCell) {
                terrain.sand = 1.0f;
                terrain.rock = 0.0f;
                continue;
            }

            bool closeToWater = false;

            for (
                int nearDq = -maxBeachDistanceFromWater;
                nearDq <= maxBeachDistanceFromWater && !closeToWater;
                ++nearDq
            ) {
                for (
                    int nearDr = -maxBeachDistanceFromWater;
                    nearDr <= maxBeachDistanceFromWater;
                    ++nearDr
                ) {
                    if (
                        hexDistanceFromOrigin(nearDq, nearDr) >
                        maxBeachDistanceFromWater
                    ) {
                        continue;
                    }

                    const TerrainSample* nearbyTerrain =
                        terrainAt(q + nearDq, r + nearDr);

                    if (nearbyTerrain == nullptr) {
                        continue;
                    }

                    bool nearbyWaterCell =
                        nearbyTerrain->height <= seaLevelLayer &&
                        nearbyTerrain->water > visibleWaterThreshold;

                    if (nearbyWaterCell) {
                        closeToWater = true;
                        break;
                    }
                }
            }

            bool lowEnoughForBeach =
                terrain.height <= seaLevelLayer + maxBeachHeightAboveSeaLevel;

            if (!closeToWater || !lowEnoughForBeach) {
                terrain.sand = 0.0f;
            } else if (terrain.height > seaLevelLayer + 1) {
                float fade =
                    1.0f -
                    static_cast<float>(terrain.height - seaLevelLayer - 1) /
                    static_cast<float>(maxBeachHeightAboveSeaLevel);
                terrain.sand = std::min(
                    terrain.sand,
                    std::clamp(fade, 0.0f, 1.0f)
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
        glm::vec3 color,
        float alpha = 1.0f
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
            color,
            alpha
        );
    };

    auto addTree = [&](int q, int r, int groundLayer) {
        float heightT = normalizedTerrainHeight(groundLayer + 1, settings);
        float elevationSizeBoost = smoothstep(0.14f, 0.56f, heightT);
        float highAltitudeShrink = smoothstep(0.54f, 0.70f, heightT);
        float sizeRoll = random01(q, r, settings.seed + 4001);
        float shapeRoll = random01(q, r, settings.seed + 4007);
        float branchRoll = random01(q, r, settings.seed + 4017);
        float crownRoll = random01(q, r, settings.seed + 4023);

        int trunkHeight = 4;
        int canopyRadius = 1;
        int canopyLayers = 2;

        if (sizeRoll < 0.13f - elevationSizeBoost * 0.05f + highAltitudeShrink * 0.14f) {
            trunkHeight = 3 + (shapeRoll > 0.60f ? 1 : 0);
            canopyLayers = 1 + (branchRoll > 0.72f ? 1 : 0);
        } else if (sizeRoll > 0.86f - elevationSizeBoost * 0.24f) {
            trunkHeight = 7 + (branchRoll > 0.48f ? 1 : 0);
            canopyLayers = 3 + (crownRoll > 0.58f ? 1 : 0);
            canopyRadius = 2;
        } else if (sizeRoll > 0.62f - elevationSizeBoost * 0.16f) {
            trunkHeight = 6;
            canopyLayers = 3;
            canopyRadius = branchRoll > 0.34f ? 2 : 1;
        } else if (shapeRoll > 0.48f - elevationSizeBoost * 0.10f) {
            trunkHeight = 5;
            canopyLayers = branchRoll > 0.38f ? 3 : 2;
        }

        if (highAltitudeShrink > 0.0f) {
            trunkHeight = std::max(
                3,
                trunkHeight - static_cast<int>(std::round(highAltitudeShrink * 2.0f))
            );
            canopyLayers = std::max(
                1,
                canopyLayers - static_cast<int>(std::round(highAltitudeShrink))
            );
        }

        int baseLayer = groundLayer + 1;

        glm::vec3 trunkColor =
            mixColor(
                glm::vec3(0.18f, 0.10f, 0.05f),
                glm::vec3(0.40f, 0.25f, 0.11f),
                random01(q, r, settings.seed + 4011)
            );
        glm::vec3 leafColor =
            mixColor(
                glm::vec3(0.04f, 0.19f, 0.07f),
                glm::vec3(0.15f, 0.47f, 0.14f),
                random01(q, r, settings.seed + 4021)
            );
        glm::vec3 leafHighlight =
            mixColor(
                leafColor,
                glm::vec3(0.24f, 0.55f, 0.18f),
                0.25f + random01(q, r, settings.seed + 4027) * 0.20f
            );
        for (int i = 0; i < trunkHeight; ++i) {
            addPrism(
                q,
                r,
                baseLayer + i,
                wrappedRotationStep(q, r, baseLayer + i, settings.seed + 31),
                trunkColor
            );
        }

        int canopyBase = baseLayer + trunkHeight;
        int canopyRotation =
            wrappedRotationStep(q, r, canopyBase, settings.seed + 57);

        auto addCanopyBlock = [&, q, r](
            int dq,
            int dr,
            int layer,
            int rotationStep,
            glm::vec3 color
        ) {
            addPrism(
                q + dq,
                r + dr,
                layer,
                rotationStep,
                color
            );
        };

        addPrism(q, r, canopyBase, canopyRotation, leafColor);

        if (canopyRadius >= 1) {
            addCanopyBlock(1, 0, canopyBase, canopyRotation + 2, leafColor);
            addCanopyBlock(-1, 0, canopyBase, canopyRotation + 3, leafColor);
            addCanopyBlock(0, 1, canopyBase, canopyRotation + 4, leafColor);
            addCanopyBlock(0, -1, canopyBase, canopyRotation + 5, leafColor);
        }

        if (canopyRadius >= 2) {
            addCanopyBlock(1, -1, canopyBase, canopyRotation + 1, leafColor);
            addCanopyBlock(-1, 1, canopyBase, canopyRotation + 2, leafColor);
            addCanopyBlock(1, 1, canopyBase, canopyRotation + 3, leafColor);
            addCanopyBlock(-1, -1, canopyBase, canopyRotation + 4, leafColor);

            if (crownRoll > 0.64f) {
                addCanopyBlock(2, 0, canopyBase, canopyRotation + 2, leafColor);
            }
            if (crownRoll < 0.36f) {
                addCanopyBlock(-2, 0, canopyBase, canopyRotation + 3, leafColor);
            }
            if (branchRoll > 0.66f) {
                addCanopyBlock(0, 2, canopyBase, canopyRotation + 4, leafColor);
            }
            if (branchRoll < 0.34f) {
                addCanopyBlock(0, -2, canopyBase, canopyRotation + 5, leafColor);
            }
        }

        if (canopyLayers >= 2) {
            addPrism(q, r, canopyBase + 1, canopyRotation + 1, leafHighlight);

            if (shapeRoll > 0.35f) {
                addCanopyBlock(1, 0, canopyBase + 1, canopyRotation + 2, leafColor);
                addCanopyBlock(-1, 0, canopyBase + 1, canopyRotation + 3, leafColor);

                if (canopyRadius >= 2 && branchRoll > 0.52f) {
                    addCanopyBlock(1, -1, canopyBase + 1, canopyRotation + 4, leafColor);
                }
            } else {
                addCanopyBlock(0, 1, canopyBase + 1, canopyRotation + 4, leafColor);
                addCanopyBlock(0, -1, canopyBase + 1, canopyRotation + 5, leafColor);

                if (canopyRadius >= 2 && branchRoll < 0.48f) {
                    addCanopyBlock(-1, 1, canopyBase + 1, canopyRotation + 2, leafColor);
                }
            }
        }

        if (canopyLayers >= 3) {
            addPrism(q, r, canopyBase + 2, canopyRotation + 2, leafHighlight);

            if (canopyRadius >= 2 && crownRoll > 0.42f) {
                addCanopyBlock(1, 0, canopyBase + 2, canopyRotation + 3, leafHighlight);
            }
            if (canopyRadius >= 2 && crownRoll < 0.58f) {
                addCanopyBlock(-1, 0, canopyBase + 2, canopyRotation + 4, leafColor);
            }
        }

        if (canopyLayers >= 4) {
            addPrism(q, r, canopyBase + 3, canopyRotation + 3, leafHighlight);
        }
    };

    std::unordered_map<uint64_t, int> treeDistanceByCell;
    treeDistanceByCell.reserve(visibleTerrainCellCount / 8u);

    constexpr int maxTreeSpacingDistance = 4;
    const std::vector<HexOffset>& treeSpacingOffsets =
        hexOffsetsWithinRadius(maxTreeSpacingDistance - 1);

    auto minimumTreeDistanceForTerrain = [
        &settings
    ](const TerrainSample& terrain) {
        float heightT = normalizedTerrainHeight(terrain.height, settings);

        if (heightT < 0.16f) {
            return 4;
        }

        if (heightT < 0.46f) {
            return 3;
        }

        return 2;
    };

    auto canPlaceTreeWithSpacing = [
        &treeDistanceByCell,
        &treeSpacingOffsets
    ](int q, int r, int requiredDistance) {
        for (const HexOffset& offset : treeSpacingOffsets) {
            auto existingTree =
                treeDistanceByCell.find(
                    coordinateKey(q + offset.dq, r + offset.dr)
                );

            if (existingTree == treeDistanceByCell.end()) {
                continue;
            }

            int existingRequiredDistance = existingTree->second;
            int distance = std::max(
                2,
                std::min(requiredDistance, existingRequiredDistance)
            );

            if (hexDistanceFromOrigin(offset.dq, offset.dr) < distance) {
                return false;
            }
        }

        return true;
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

            if (shouldPlaceTree(q, r, *terrain, settings)) {
                int requiredTreeDistance =
                    minimumTreeDistanceForTerrain(*terrain);

                if (canPlaceTreeWithSpacing(q, r, requiredTreeDistance)) {
                    treeDistanceByCell.emplace(
                        coordinateKey(q, r),
                        requiredTreeDistance
                    );
                    addTree(q, r, terrainHeight - 1);
                }
            }
        }
    }

    constexpr float waterAlpha = 0.58f;

    for (int dq = -renderRadius; dq <= renderRadius; ++dq) {
        for (int dr = -renderRadius; dr <= renderRadius; ++dr) {
            if (hexDistanceFromOrigin(dq, dr) > renderRadius) {
                continue;
            }

            int q = center.q + dq;
            int r = center.r + dr;
            const TerrainSample* terrain = terrainAt(q, r);

            if (terrain == nullptr) {
                continue;
            }

            bool isWaterSurface =
                terrain->height <= seaLevelLayer &&
                terrain->water > visibleWaterThreshold;

            if (!isWaterSurface) {
                continue;
            }

            addPrism(
                q,
                r,
                seaLevelLayer,
                wrappedRotationStep(q, r, seaLevelLayer, settings.seed + 9091),
                waterColorForTerrain(q, r, *terrain, settings),
                waterAlpha
            );
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
