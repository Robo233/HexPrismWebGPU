#include "Camera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

glm::vec3 getCameraForward(const Camera& camera) {
    float yawRad = glm::radians(camera.yaw);
    float pitchRad = glm::radians(camera.pitch);

    glm::vec3 forward{};
    forward.x = std::cos(pitchRad) * std::sin(yawRad);
    forward.y = std::sin(pitchRad);
    forward.z = -std::cos(pitchRad) * std::cos(yawRad);

    return glm::normalize(forward);
}

glm::vec3 getCameraRight(const Camera& camera) {
    glm::vec3 forward = getCameraForward(camera);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    return glm::normalize(glm::cross(forward, worldUp));
}

void rotateCamera(
    Camera& camera,
    float yawDelta,
    float pitchDelta
) {
    camera.yaw += yawDelta;
    camera.pitch += pitchDelta;

    camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
}