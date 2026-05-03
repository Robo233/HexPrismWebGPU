#pragma once

#include "HexGrid.hpp"
#include "Prism.hpp"
#include "Settings.hpp"

#include <glm/glm.hpp>

#include <vector>

struct ProceduralTerrainSettings {
    int seed = Settings::Terrain::seed;
    int renderRadius = Settings::Terrain::renderRadius;
    int rebuildStride = Settings::Terrain::rebuildStride;
    int minTerrainHeight = Settings::Terrain::minHeight;
    int maxTerrainHeight = Settings::Terrain::maxHeight;
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
