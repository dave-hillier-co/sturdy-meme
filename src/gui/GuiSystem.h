#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

#include "GuiIKTab.h"
#include "GuiPlayerTab.h"
#include "GuiEnvironmentTab.h"
#include "GuiTileLoaderTab.h"
#include "GuiDashboard.h"
#include "GuiInterfaces.h"
#include "GuiPanelRegistry.h"
#include "SceneEditorState.h"

class Camera;
typedef unsigned int ImGuiID;

class GuiSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GuiSystem(ConstructToken) {}

    /**
     * Factory: Create and initialize GUI system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GuiSystem> create(SDL_Window* window, vk::Instance instance,
                                              vk::PhysicalDevice physicalDevice, vk::Device device,
                                              uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                                              vk::RenderPass renderPass, uint32_t imageCount);

    ~GuiSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    GuiSystem(GuiSystem&&) = delete;
    GuiSystem& operator=(GuiSystem&&) = delete;
    GuiSystem(const GuiSystem&) = delete;
    GuiSystem& operator=(const GuiSystem&) = delete;

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void render(GuiInterfaces& interfaces, const Camera& camera, float deltaTime, float fps);
    void endFrame(vk::CommandBuffer cmd);
    void cancelFrame();  // End frame without rendering (for early returns)

    bool wantsInput() const;
    bool isVisible() const { return visible; }
    void toggleVisibility() { visible = !visible; }
    void setVisible(bool v) { visible = v; }

    // Get player settings for external systems
    PlayerSettings& getPlayerSettings() { return playerSettings; }
    const PlayerSettings& getPlayerSettings() const { return playerSettings; }

    // Check if gizmo is being used (for input blocking)
    bool isGizmoActive() const;

private:
    bool initInternal(SDL_Window* window, vk::Instance instance, vk::PhysicalDevice physicalDevice,
                      vk::Device device, uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                      vk::RenderPass renderPass, uint32_t imageCount);
    void cleanup();

    void buildPanelRegistry();
    void renderMainMenuBar();
    void applyDefaultDockLayout(ImGuiID dockspaceId);

    vk::Device device_ = VK_NULL_HANDLE;  // Stored for cleanup
    vk::DescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;

    // IK debug settings
    IKDebugSettings ikDebugSettings;

    // Player settings
    PlayerSettings playerSettings;

    // Environment tab state
    EnvironmentTabState environmentTabState;

    // Scene editor state (hierarchy + inspector + gizmo)
    SceneEditorState sceneEditorState;

    // Dashboard state (frame time tracking)
    GuiDashboard::State dashboardState;

    // Tile loader state
    GuiTileLoaderTab::State tileLoaderState;

    // Panel registry: one entry per menu-toggleable debug panel
    std::vector<PanelDesc> panels_;

    // Special-cased dockable editor panels (inline handling + gizmo)
    bool showHierarchy_ = false;
    bool showInspector_ = false;

    // Layout persistence: stable ini path (ImGui keeps the pointer)
    std::string iniFilePath_;
    bool applyDefaultLayout_ = false;   // true when no ini existed at startup
    bool dockLayoutInitialized_ = false;
};
