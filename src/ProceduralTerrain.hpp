#pragma once

#include "HexGrid.hpp"
#include "Prism.hpp"

#include <glm/glm.hpp>

#include <vector>

struct ProceduralTerrainSettings {
    int seed = 2;
    int renderRadius = 200;
    int rebuildStride = 4;
    int minTerrainHeight = 1;
    int maxTerrainHeight = 50;
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
