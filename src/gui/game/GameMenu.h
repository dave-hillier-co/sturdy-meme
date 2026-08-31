#pragma once

#include <SDL3/SDL.h>
#include <functional>

class InputSystem;
class PerformanceToggles;

/**
 * GameMenu - Player-facing pause menu and settings screen.
 *
 * Separate from the F1 debug dashboard: rendered every frame by GuiSystem
 * regardless of the debug GUI visibility flag. Owns its own state machine
 * (Hidden / PauseRoot / Settings) and is wired to the rest of the app
 * through a small Hooks struct set once by Application.
 *
 * Systems that may not exist yet (or may rebind) are looked up per frame
 * through the std::function accessors in Hooks rather than cached.
 */
class GameMenu {
public:
    struct Hooks {
        InputSystem* input = nullptr;   // sensitivity / invert-Y / mouse-look mode
        SDL_Window* window = nullptr;   // fullscreen toggle
        // Per-frame lookup; may return nullptr while systems are still loading
        std::function<PerformanceToggles*()> performanceToggles;
        std::function<void()> requestQuit;
    };

    void setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

    // Draw the overlay + menu window (no-op while hidden). Call every frame
    // inside the ImGui frame, independent of debug-GUI visibility.
    void render();

    // Esc semantics: Hidden -> open pause root; Settings -> back to pause
    // root; PauseRoot -> close.
    void handleEscape();

    bool isOpen() const { return state_ != State::Hidden; }
    void open();
    void close();

private:
    enum class State { Hidden, PauseRoot, Settings };

    void renderPauseRoot();
    void renderSettings();

    State state_ = State::Hidden;
    Hooks hooks_;
    bool restoreMouseLook_ = false;  // mouse-look was active when the menu opened
};
