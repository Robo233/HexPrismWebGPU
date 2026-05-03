#include "InputController.hpp"

#include <glm/glm.hpp>

static bool isCameraUpsideDown(const Camera& camera) {
    constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    return glm::dot(getCameraUp(camera), worldUp) < 0.0f;
}

void InputController::update(
    SDL_Window* window,
    Camera& camera,
    float deltaTime,
    bool& running
) {
    SDL_Event event{};

    while (SDL_PollEvent(&event)) {
        handleEvent(window, event, camera, running);
    }

    handleKeyboard(window, camera, deltaTime, running);
}

void InputController::handleEvent(
    SDL_Window* window,
    const SDL_Event& event,
    Camera& camera,
    bool& running
) {
    if (event.type == SDL_EVENT_QUIT) {
        running = false;
        return;
    }

    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (
            event.button.button == SDL_BUTTON_LEFT ||
            event.button.button == SDL_BUTTON_RIGHT
        ) {
            mouseLookEnabled_ = true;
            SDL_SetWindowRelativeMouseMode(window, true);
        }
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION && mouseLookEnabled_) {
        constexpr float mouseSensitivity = 0.12f;
        float rollSign = isCameraUpsideDown(camera) ? -1.0f : 1.0f;

        float yawDelta =
            event.motion.xrel * mouseSensitivity * rollSign;
        float pitchDelta =
            -event.motion.yrel * mouseSensitivity;

        rotateCamera(camera, yawDelta, pitchDelta);
    }
}

void InputController::handleKeyboard(
    SDL_Window* window,
    Camera& camera,
    float deltaTime,
    bool& running
) {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    float moveSpeed = 15.0f;
    float rotateSpeed = 90.0f;

    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        moveSpeed *= 2.5f;
    }

    glm::vec3 forward = getCameraForward(camera);
    glm::vec3 right = getCameraRight(camera);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float rollSign = isCameraUpsideDown(camera) ? -1.0f : 1.0f;
    glm::vec3 vertical = worldUp * rollSign;

    float moveAmount = moveSpeed * deltaTime;
    float rotateAmount = rotateSpeed * deltaTime;

    if (keys[SDL_SCANCODE_ESCAPE]) {
        if (mouseLookEnabled_) {
            mouseLookEnabled_ = false;
            SDL_SetWindowRelativeMouseMode(window, false);
        }

        running = false;
    }

    if (keys[SDL_SCANCODE_W]) {
        camera.position += forward * moveAmount;
    }

    if (keys[SDL_SCANCODE_S]) {
        camera.position -= forward * moveAmount;
    }

    if (keys[SDL_SCANCODE_A]) {
        camera.position -= right * moveAmount;
    }

    if (keys[SDL_SCANCODE_D]) {
        camera.position += right * moveAmount;
    }

    // Swapped as requested:
    // Ctrl moves up, Space moves down.
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) {
        camera.position += vertical * moveAmount;
    }

    if (keys[SDL_SCANCODE_SPACE]) {
        camera.position -= vertical * moveAmount;
    }

    if (keys[SDL_SCANCODE_LEFT]) {
        rotateCamera(camera, -rotateAmount * rollSign, 0.0f);
    }

    if (keys[SDL_SCANCODE_RIGHT]) {
        rotateCamera(camera, rotateAmount * rollSign, 0.0f);
    }

    if (keys[SDL_SCANCODE_UP]) {
        rotateCamera(camera, 0.0f, rotateAmount * rollSign);
    }

    if (keys[SDL_SCANCODE_DOWN]) {
        rotateCamera(camera, 0.0f, -rotateAmount * rollSign);
    }
}
