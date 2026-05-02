#pragma once

#include "Prism.hpp"

#include <glm/glm.hpp>

struct HexCell {
    int q = 0;
    int r = 0;
    int y = 0;
};

struct HexGridMetrics {
    float hexRadius = 0.5f;
    float prismHeight = 0.5f;

    float flatToFlat() const;
    float pointToPoint() const;
    float horizontalSpacing() const;
    float diagonalSpacing() const;
};

glm::vec3 hexCellToWorld(
    HexCell cell,
    const HexGridMetrics& metrics = HexGridMetrics{}
);

Prism makePrismAtHexCell(
    int q,
    int r,
    int y = 0,
    int rotationStep = 0,
    const HexGridMetrics& metrics = HexGridMetrics{}
);

Prism makePrismAtHexCell(
    int q,
    int r,
    int y,
    int rotationStep,
    glm::vec3 color,
    const HexGridMetrics& metrics = HexGridMetrics{}
);

Prism makePrismAtHexCell(
    HexCell cell,
    int rotationStep = 0,
    const HexGridMetrics& metrics = HexGridMetrics{}
);

Prism makePrismAtHexCell(
    HexCell cell,
    int rotationStep,
    glm::vec3 color,
    const HexGridMetrics& metrics = HexGridMetrics{}
);
