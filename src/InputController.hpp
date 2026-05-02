#pragma once

#include "Camera.hpp"

#include <SDL3/SDL.h>

class InputController {
public:
    void update(
        SDL_Window* window,
        Camera& camera,
        float deltaTime,
        bool& running
    );

private:
    bool rightMouseDown_ = false;

    void handleEvent(
        SDL_Window* window,
        const SDL_Event& event,
        Camera& camera,
        bool& running
    );

    void handleKeyboard(
        Camera& camera,
        float deltaTime,
        bool& running
    );
};