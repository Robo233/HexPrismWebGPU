#pragma once

#include <cstdint>
#include <vector>

struct PrismVertex {
    float px, py, pz;
    float nx, ny, nz;
};

struct MeshData {
    std::vector<PrismVertex> vertices;
    std::vector<uint16_t> indices;
};

MeshData createHexPrismMesh();
