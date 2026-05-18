#include "HexPrismMesh.hpp"

#include <array>
#include <cmath>

#include <glm/glm.hpp>

static uint16_t addVertex(
    std::vector<PrismVertex>& vertices,
    glm::vec3 position,
    glm::vec3 normal
) {
    uint16_t index = static_cast<uint16_t>(vertices.size());

    vertices.push_back({
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z
    });

    return index;
}

static void addIndexedTriangle(
    std::vector<uint16_t>& indices,
    uint16_t a,
    uint16_t b,
    uint16_t c
) {
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
}

static void addQuad(
    std::vector<PrismVertex>& vertices,
    std::vector<uint16_t>& indices,
    glm::vec3 a,
    glm::vec3 b,
    glm::vec3 c,
    glm::vec3 d
) {
    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    uint16_t base = static_cast<uint16_t>(vertices.size());

    addVertex(vertices, a, normal);
    addVertex(vertices, b, normal);
    addVertex(vertices, c, normal);
    addVertex(vertices, d, normal);

    addIndexedTriangle(indices, base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2));
    addIndexedTriangle(indices, base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3));
}

MeshData createHexPrismMesh() {
    MeshData mesh{};

    constexpr float hexSideLength = 0.5f;
    constexpr float radius = hexSideLength;
    constexpr float depth = hexSideLength;
    constexpr float twoPi = 6.28318530718f;

    std::array<glm::vec3, 6> front{};
    std::array<glm::vec3, 6> back{};
    std::array<uint16_t, 6> frontIndices{};

    mesh.vertices.reserve(30);
    mesh.indices.reserve(48);

    for (int i = 0; i < 6; ++i) {
        float a = static_cast<float>(i) / 6.0f * twoPi;

        float x = std::cos(a) * radius;
        float y = std::sin(a) * radius;

        front[i] = glm::vec3(x, y, depth * 0.5f);
        back[i] = glm::vec3(x, y, -depth * 0.5f);
        frontIndices[i] =
            addVertex(
                mesh.vertices,
                front[i],
                glm::vec3(0.0f, 0.0f, 1.0f)
            );
    }

    // Top cap. Terrain never exposes prism bottoms, so the bottom cap is
    // omitted from the shared mesh.
    for (int i = 1; i < 5; ++i) {
        addIndexedTriangle(
            mesh.indices,
            frontIndices[0],
            frontIndices[i],
            frontIndices[i + 1]
        );
    }

    // Side faces.
    for (int i = 0; i < 6; ++i) {
        int next = (i + 1) % 6;

        addQuad(
            mesh.vertices,
            mesh.indices,
            front[i],
            back[i],
            back[next],
            front[next]
        );
    }

    return mesh;
}
