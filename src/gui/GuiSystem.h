#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

#include "GuiDebugTab.h"
#include "GuiPlayerTab.h"
#include "GuiPanelRegistry.h"
#include "SceneEditorState.h"

class Camera;
class RendererSystems;
class PhysicsTerrainTileManager;
struct DebugCommand;
class GuiDashboard;
class GuiEnvironmentTab;
class GuiIKTab;
class GuiTileLoaderTab;
typedef unsigned int ImGuiID;

class GuiSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    // Defined in the .cpp: member unique_ptrs hold forward-declared panel types
    explicit GuiSystem(ConstructToken);

    /**
     * Factory: Create and initialize GUI system.
     * Panels bind their (narrow) system dependencies from `systems` at
     * construction; `debugHooks` supplies Application-level debug actions,
     * `physicsTerrainTiles` (nullable) and `debugCommands` (nullable, but
     * with stable address) feed the tile-loader and debug panels.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GuiSystem> create(SDL_Window* window, vk::Instance instance,
                                              vk::PhysicalDevice physicalDevice, vk::Device device,
                                              uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                                              vk::RenderPass renderPass, uint32_t imageCount,
                                              RendererSystems& systems,
                                              GuiDebugTab::Hooks debugHooks,
                                              PhysicsTerrainTileManager* physicsTerrainTiles,
                                              const std::vector<DebugCommand>* debugCommands);

    ~GuiSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    GuiSystem(GuiSystem&&) = delete;
    GuiSystem& operator=(GuiSystem&&) = delete;
    GuiSystem(const GuiSystem&) = delete;
    GuiSystem& operator=(const GuiSystem&) = delete;

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void render(const Camera& camera, float deltaTime, float fps);
    void endFrame(vk::CommandBuffer cmd);
    void cancelFrame();  // End frame without rendering (for early returns)

    bool wantsInput() const;
    bool isVisible() const { return visible; }
    void toggleVisibility() { visible = !visible; }
    void setVisible(bool v) { visible = v; }

    // Get player settings for external systems (owned by the Player panel)
    PlayerSettings& getPlayerSettings();
    const PlayerSettings& getPlayerSettings() const;

    // Check if gizmo is being used (for input blocking)
    bool isGizmoActive() const;

private:
    bool initInternal(SDL_Window* window, vk::Instance instance, vk::PhysicalDevice physicalDevice,
                      vk::Device device, uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                      vk::RenderPass renderPass, uint32_t imageCount);
    void cleanup();

    void buildPanelRegistry(GuiDebugTab::Hooks debugHooks,
                            PhysicsTerrainTileManager* physicsTerrainTiles,
                            const std::vector<DebugCommand>* debugCommands);
    void renderMainMenuBar();
    void applyDefaultDockLayout(ImGuiID dockspaceId);

    vk::Device device_ = VK_NULL_HANDLE;  // Stored for cleanup
    vk::DescriptorPool imguiPool = VK_NULL_HANDLE;
    bool visible = true;

    // Stable systems accessor. Used for late-bound dependencies (ECS world,
    // tree/ocean systems created after init) and for the parallel-lane
    // scene editor panels.
    RendererSystems* systems_ = nullptr;

    // Panel objects: each owns its persistent state, with dependencies bound
    // at construction (built in buildPanelRegistry).
    std::unique_ptr<GuiDashboard> dashboard_;
    std::unique_ptr<GuiEnvironmentTab> environmentTab_;
    std::unique_ptr<GuiPlayerTab> playerTab_;
    std::unique_ptr<GuiIKTab> ikTab_;
    std::unique_ptr<GuiDebugTab> debugTab_;
    std::unique_ptr<GuiTileLoaderTab> tileLoaderTab_;

    // Scene editor state (hierarchy + inspector + gizmo)
    SceneEditorState sceneEditorState;

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
