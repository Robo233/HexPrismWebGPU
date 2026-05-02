#include "HexGrid.hpp"

#include <cmath>

float HexGridMetrics::flatToFlat() const {
    return std::sqrt(3.0f) * hexRadius;
}

float HexGridMetrics::pointToPoint() const {
    return 2.0f * hexRadius;
}

float HexGridMetrics::horizontalSpacing() const {
    return 1.5f * hexRadius;
}

float HexGridMetrics::diagonalSpacing() const {
    return flatToFlat();
}

glm::vec3 hexCellToWorld(
    HexCell cell,
    const HexGridMetrics& metrics
) {
    // Flat-top axial hex coordinates.
    //
    // q moves horizontally.
    // r moves diagonally.
    // y stacks prisms vertically.
    //
    // World axes:
    // X = horizontal
    // Y = vertical
    // Z = depth / diagonal hex-grid axis

    float x =
        metrics.horizontalSpacing() *
        static_cast<float>(cell.q);

    float z =
        metrics.diagonalSpacing() *
        (
            static_cast<float>(cell.r) +
            static_cast<float>(cell.q) * 0.5f
        );

    float y =
        metrics.prismHeight *
        static_cast<float>(cell.y);

    return glm::vec3(x, y, z);
}

Prism makePrismAtHexCell(
    int q,
    int r,
    int y,
    int rotationStep,
    const HexGridMetrics& metrics
) {
    return makePrismAtHexCell(
        HexCell{ q, r, y },
        rotationStep,
        metrics
    );
}

Prism makePrismAtHexCell(
    int q,
    int r,
    int y,
    int rotationStep,
    glm::vec3 color,
    const HexGridMetrics& metrics
) {
    return makePrismAtHexCell(
        HexCell{ q, r, y },
        rotationStep,
        color,
        metrics
    );
}

Prism makePrismAtHexCell(
    HexCell cell,
    int rotationStep,
    const HexGridMetrics& metrics
) {
    return Prism(
        hexCellToWorld(cell, metrics),
        rotationStep
    );
}

Prism makePrismAtHexCell(
    HexCell cell,
    int rotationStep,
    glm::vec3 color,
    const HexGridMetrics& metrics
) {
    return Prism(
        hexCellToWorld(cell, metrics),
        rotationStep,
        color
    );
}
