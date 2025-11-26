#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "Renderer.h"
#include "Camera.h"
#include "Player.h"
#include "PhysicsWorld.h"

class Application {
public:
    Application() = default;
    ~Application() = default;

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

private:
    void handleInput(float deltaTime);
    void handleGamepadInput(float deltaTime);
    void handleFreeCameraInput(float deltaTime, const bool* keyState);
    void handleThirdPersonInput(float deltaTime, const bool* keyState);
    void handleFreeCameraGamepadInput(float deltaTime);
    void handleThirdPersonGamepadInput(float deltaTime);
    void processEvents();
    void openGamepad(SDL_JoystickID id);
    void closeGamepad();
    std::string getResourcePath();
    void setupPhysicsScene();

    SDL_Window* window = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    Renderer renderer;
    Camera camera;
    Player player;
    PhysicsWorld physicsWorld;

    // Physics-driven movement
    glm::vec3 playerVelocity{0.0f};
    bool wantsJump = false;

    bool running = false;
    bool thirdPersonMode = false;  // Toggle between free camera and third-person
    float moveSpeed = 3.0f;
    float rotateSpeed = 60.0f;

    static constexpr float stickDeadzone = 0.15f;
    static constexpr float gamepadLookSpeed = 120.0f;
};
