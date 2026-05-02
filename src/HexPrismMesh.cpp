#include "HexPrismMesh.hpp"

#include <array>
#include <cmath>

#include <glm/glm.hpp>

static void addVertex(
    std::vector<PrismVertex>& vertices,
    glm::vec3 position,
    glm::vec3 normal
) {
    vertices.push_back({
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z
    });
}

static void addTriangle(
    std::vector<PrismVertex>& vertices,
    std::vector<uint16_t>& indices,
    glm::vec3 a,
    glm::vec3 b,
    glm::vec3 c
) {
    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));

    uint16_t base = static_cast<uint16_t>(vertices.size());

    addVertex(vertices, a, normal);
    addVertex(vertices, b, normal);
    addVertex(vertices, c, normal);

    indices.push_back(base);
    indices.push_back(static_cast<uint16_t>(base + 1));
    indices.push_back(static_cast<uint16_t>(base + 2));
}

static void addQuad(
    std::vector<PrismVertex>& vertices,
    std::vector<uint16_t>& indices,
    glm::vec3 a,
    glm::vec3 b,
    glm::vec3 c,
    glm::vec3 d
) {
    addTriangle(vertices, indices, a, b, c);
    addTriangle(vertices, indices, a, c, d);
}

MeshData createHexPrismMesh() {
    MeshData mesh{};

constexpr float hexSideLength = 0.5f;
constexpr float radius = hexSideLength;
constexpr float depth = hexSideLength;
constexpr float twoPi = 6.28318530718f;

    std::array<glm::vec3, 6> front{};
    std::array<glm::vec3, 6> back{};

    for (int i = 0; i < 6; ++i) {
        float a = static_cast<float>(i) / 6.0f * twoPi;

        float x = std::cos(a) * radius;
        float y = std::sin(a) * radius;

        front[i] = glm::vec3(x, y, depth * 0.5f);
        back[i] = glm::vec3(x, y, -depth * 0.5f);
    }

    // Front cap.
    for (int i = 1; i < 5; ++i) {
        addTriangle(
            mesh.vertices,
            mesh.indices,
            front[0],
            front[i],
            front[i + 1]
        );
    }

    // Back cap, reversed so normal points backward.
    for (int i = 1; i < 5; ++i) {
        addTriangle(
            mesh.vertices,
            mesh.indices,
            back[0],
            back[i + 1],
            back[i]
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
