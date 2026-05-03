#pragma once

#include "HexGrid.hpp"
#include "Prism.hpp"

#include <glm/glm.hpp>

#include <vector>

struct ProceduralTerrainSettings {
    int seed = 6564;
    int renderRadius = 400;
    int rebuildStride = 16;
    int minTerrainHeight = 1;
    int maxTerrainHeight = 256;
};

struct TerrainBuildResult {
    HexCell center;
    std::vector<Prism> prisms;
};

HexCell renderCenterForPosition(
    glm::vec3 position,
    const ProceduralTerrainSettings& settings
);

bool sameRenderCenter(HexCell a, HexCell b);

int hexDistanceBetweenCells(HexCell a, HexCell b);

TerrainBuildResult buildProceduralTerrain(
    HexCell center,
    ProceduralTerrainSettings settings
);
