#pragma once

#include <glm/glm.hpp>

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 0.4f, 3.2f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

glm::vec3 getCameraForward(const Camera& camera);
glm::vec3 getCameraRight(const Camera& camera);
glm::vec3 getCameraUp(const Camera& camera);

void rotateCamera(
    Camera& camera,
    float yawDelta,
    float pitchDelta
);
