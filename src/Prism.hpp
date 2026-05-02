#pragma once

#include <glm/glm.hpp>

struct Prism {
    glm::vec3 position;
    int rotationStep;
    glm::vec3 color;

    explicit Prism(
        glm::vec3 position = glm::vec3(0.0f),
        int rotationStep = 0,
        glm::vec3 color = glm::vec3(1.0f)
    )
        : position(position),
          rotationStep(rotationStep),
          color(color) {}
};
