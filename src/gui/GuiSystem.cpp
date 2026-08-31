#include "GuiSystem.h"
#include "Camera.h"

#include "core/RendererSystems.h"
#include "TimeSystem.h"  // TimeSystem& -> ITimeSystem& conversions need the complete type

// GUI module headers
#include "GuiStyle.h"
#include "GuiDashboard.h"
#include "GuiPositionPanel.h"
#include "GuiTileLoaderTab.h"
#include "GuiTimeTab.h"
#include "GuiWeatherTab.h"
#include "GuiEnvironmentTab.h"
#include "GuiPostFXTab.h"
#include "GuiTerrainTab.h"
#include "GuiWaterTab.h"
#include "GuiDebugTab.h"
#include "GuiProfilerTab.h"
#include "GuiPerformanceTab.h"
#include "GuiIKTab.h"
#include "GuiPlayerTab.h"
#include "GuiNPCTab.h"
#include "GuiTreeTab.h"
#include "GuiGrassTab.h"
#include "GuiHierarchyPanel.h"
#include "GuiInspectorPanel.h"
#include "GuiGizmo.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

#include <filesystem>

static void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        SDL_Log("ImGui Vulkan Error: VkResult = %d", err);
    }
}

namespace {

const char* menuCategoryName(MenuCategory category) {
    switch (category) {
        case MenuCategory::View: return "View";
        case MenuCategory::Environment: return "Environment";
        case MenuCategory::Rendering: return "Rendering";
        case MenuCategory::Character: return "Character";
        case MenuCategory::Scene: return "Scene";
        case MenuCategory::Debug: return "Debug";
    }
    return "";
}

constexpr MenuCategory kMenuOrder[] = {
    MenuCategory::View,
    MenuCategory::Environment,
    MenuCategory::Rendering,
    MenuCategory::Character,
    MenuCategory::Scene,
    MenuCategory::Debug,
};

} // anonymous namespace

// Factory
std::unique_ptr<GuiSystem> GuiSystem::create(SDL_Window* window, vk::Instance instance,
                                              vk::PhysicalDevice physicalDevice, vk::Device device,
                                              uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                                              vk::RenderPass renderPass, uint32_t imageCount,
                                              RendererSystems& systems,
                                              GuiDebugTab::Hooks debugHooks,
                                              PhysicsTerrainTileManager* physicsTerrainTiles,
                                              const std::vector<DebugCommand>* debugCommands) {
    auto gui = std::make_unique<GuiSystem>(ConstructToken{});
    gui->systems_ = &systems;
    if (!gui->initInternal(window, instance, physicalDevice, device, graphicsQueueFamily,
                           graphicsQueue, renderPass, imageCount)) {
        return nullptr;
    }
    gui->buildPanelRegistry(std::move(debugHooks), physicsTerrainTiles, debugCommands);
    return gui;
}

GuiSystem::GuiSystem(ConstructToken) {}

// Destructor
GuiSystem::~GuiSystem() {
    cleanup();
}

bool GuiSystem::initInternal(SDL_Window* window, vk::Instance instance, vk::PhysicalDevice physicalDevice,
                              vk::Device device, uint32_t graphicsQueueFamily, vk::Queue graphicsQueue,
                              vk::RenderPass renderPass, uint32_t imageCount) {
    device_ = device;  // Store for cleanup

    // Create descriptor pool for ImGui
    std::array<vk::DescriptorPoolSize, 11> poolSizes = {{
        {vk::DescriptorType::eSampler, 1000},
        {vk::DescriptorType::eCombinedImageSampler, 1000},
        {vk::DescriptorType::eSampledImage, 1000},
        {vk::DescriptorType::eStorageImage, 1000},
        {vk::DescriptorType::eUniformTexelBuffer, 1000},
        {vk::DescriptorType::eStorageTexelBuffer, 1000},
        {vk::DescriptorType::eUniformBuffer, 1000},
        {vk::DescriptorType::eStorageBuffer, 1000},
        {vk::DescriptorType::eUniformBufferDynamic, 1000},
        {vk::DescriptorType::eStorageBufferDynamic, 1000},
        {vk::DescriptorType::eInputAttachment, 1000}
    }};

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .setMaxSets(1000)
        .setPoolSizes(poolSizes);

    vk::Device vkDevice(device);
    try {
        imguiPool = vkDevice.createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_Log("Failed to create ImGui descriptor pool: %s", e.what());
        return false;
    }

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Persist window layout to a deterministic path next to the executable.
    // The std::string member keeps the pointer alive for the ImGui context.
    const char* basePath = SDL_GetBasePath();
    iniFilePath_ = basePath ? std::string(basePath) + "imgui_layout.ini"
                            : std::string("imgui_layout.ini");
    io.IniFilename = iniFilePath_.c_str();

    // First run (no saved layout): build a default dock layout on first render
    std::error_code ec;
    applyDefaultLayout_ = !std::filesystem::exists(iniFilePath_, ec);

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    // Since ImGui 1.92 (2025/09/26) the render pass / MSAA / subpass settings
    // live in the per-viewport PipelineInfoMain struct rather than directly on
    // InitInfo. Guard so the code builds against both 1.91.x and 1.92+.
#if IMGUI_VERSION_NUM >= 19200
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PipelineInfoMain.RenderPass = renderPass;
#else
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.RenderPass = renderPass;
#endif
    initInfo.CheckVkResultFn = checkVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        SDL_Log("Failed to initialize ImGui Vulkan backend");
        return false;
    }

    // Setup custom style
    GuiStyle::apply();

    SDL_Log("ImGui initialized successfully");
    return true;
}

void GuiSystem::buildPanelRegistry(GuiDebugTab::Hooks debugHooks,
                                   PhysicsTerrainTileManager* physicsTerrainTiles,
                                   const std::vector<DebugCommand>* debugCommands) {
    RendererSystems& systems = *systems_;
    panels_.clear();

    // Panel objects own their persistent state; dependencies that are stable
    // for the GUI's lifetime are bound here, once.
    dashboard_ = std::make_unique<GuiDashboard>(systems.terrain(), systems.time());
    environmentTab_ = std::make_unique<GuiEnvironmentTab>(systems.environmentControl());
    playerTab_ = std::make_unique<GuiPlayerTab>(systems.playerControl());
    ikTab_ = std::make_unique<GuiIKTab>(systems.sceneControl());
    debugTab_ = std::make_unique<GuiDebugTab>(systems.debugControl(), std::move(debugHooks),
                                              debugCommands);
    tileLoaderTab_ = std::make_unique<GuiTileLoaderTab>(systems.terrain(), physicsTerrainTiles);

    // View
    panels_.push_back({"Dashboard", MenuCategory::View,
        [this](const GuiFrameContext& ctx) { dashboard_->draw(ctx); }, true});
    panels_.push_back({"Position", MenuCategory::View,
        [](const GuiFrameContext& ctx) { GuiPositionPanel::render(ctx.camera); }, true});

    // Environment
    panels_.push_back({"Time", MenuCategory::Environment,
        [&time = systems.time(), &location = systems.locationControl()](const GuiFrameContext&) {
            GuiTimeTab::render(time, location);
        }});
    panels_.push_back({"Weather", MenuCategory::Environment,
        [&weather = systems.weatherState(),
         &env = systems.environmentSettings()](const GuiFrameContext&) {
            GuiWeatherTab::render(weather, env);
        }});
    panels_.push_back({"Atmosphere", MenuCategory::Environment,
        [this](const GuiFrameContext&) { environmentTab_->draw(); }});

    // Rendering
    panels_.push_back({"Post FX", MenuCategory::Rendering,
        [&postProcess = systems.postProcessState(),
         &cloudShadow = systems.cloudShadowControl()](const GuiFrameContext&) {
            GuiPostFXTab::render(postProcess, cloudShadow);
        }});
    panels_.push_back({"Terrain", MenuCategory::Rendering,
        [&terrain = systems.terrain()](const GuiFrameContext&) {
            GuiTerrainTab::render(terrain);
        }});
    panels_.push_back({"Water", MenuCategory::Rendering,
        [this](const GuiFrameContext&) {
            // OceanFFT is optional and created late (setOceanFFT); look it up
            // per frame instead of caching a pointer at construction.
            RendererSystems& sys = *systems_;
            GuiWaterTab::render(sys.water(), sys.waterTileCull(),
                                sys.hasOceanFFT() ? &sys.oceanFFT() : nullptr);
        }});
    panels_.push_back({"Trees", MenuCategory::Rendering,
        [this](const GuiFrameContext&) {
            // TreeSystem/TreeLODSystem arrive with deferred world content;
            // GuiTreeTab looks them up through RendererSystems every frame.
            GuiTreeTab::render(*systems_);
        }});
    panels_.push_back({"Grass", MenuCategory::Rendering,
        [&grass = systems.grassControl()](const GuiFrameContext&) {
            GuiGrassTab::render(grass);
        }});

    // Character
    panels_.push_back({"Player", MenuCategory::Character,
        [this](const GuiFrameContext&) { playerTab_->draw(); }});
    panels_.push_back({"NPC LOD", MenuCategory::Character,
        [&player = systems.playerControl()](const GuiFrameContext&) {
            GuiNPCTab::render(player);
        }});
    panels_.push_back({"IK / Animation", MenuCategory::Character,
        [this](const GuiFrameContext& ctx) { ikTab_->draw(ctx.camera); }});

    // Scene: Hierarchy and Inspector are special-cased outside the registry.
    // The former "Scene Graph" panel is now the "Renderables" tab inside Hierarchy.

    // Debug
    panels_.push_back({"Debug Visualizations", MenuCategory::Debug,
        [this](const GuiFrameContext&) { debugTab_->draw(); }});
    panels_.push_back({"Performance Toggles", MenuCategory::Debug,
        [&toggles = systems.performanceToggles()](const GuiFrameContext&) {
            GuiPerformanceTab::render(toggles);
        }});
    panels_.push_back({"Profiler", MenuCategory::Debug,
        [&profiler = systems.profiler()](const GuiFrameContext&) {
            GuiProfilerTab::render(profiler);
        }});
    panels_.push_back({"Tile Loader", MenuCategory::Debug,
        [this](const GuiFrameContext& ctx) { tileLoaderTab_->draw(ctx); }});
}

void GuiSystem::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;  // Not initialized or already cleaned up

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (imguiPool != VK_NULL_HANDLE) {
        vk::Device(device_).destroyDescriptorPool(imguiPool);
        imguiPool = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

void GuiSystem::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void GuiSystem::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

PlayerSettings& GuiSystem::getPlayerSettings() {
    return playerTab_->settings();
}

const PlayerSettings& GuiSystem::getPlayerSettings() const {
    return playerTab_->settings();
}

void GuiSystem::applyDefaultDockLayout(ImGuiID dockspaceId) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

    // Hierarchy docked left, Inspector docked right, Dashboard docked bottom
    ImGuiID dockMain = dockspaceId;
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.22f, nullptr, &dockMain);
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Dashboard", dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

void GuiSystem::render(const Camera& camera, float deltaTime, float fps) {
    // Debug dashboard respects the F1 visibility flag; the player-facing
    // pause menu is drawn every frame regardless (the ImGui frame always
    // runs: beginFrame/endFrame are unconditional in the main loop).
    renderDebugUi(camera, deltaTime, fps);
    gameMenu_.render();
}

void GuiSystem::renderDebugUi(const Camera& camera, float deltaTime, float fps) {
    if (!visible) return;

    // Create main viewport dockspace - allows all windows to be freely dockable
    ImGuiID mainDockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);

    // First run only (no saved layout): build the default dock layout
    if (!dockLayoutInitialized_) {
        if (applyDefaultLayout_) {
            applyDefaultDockLayout(mainDockspaceId);
        }
        dockLayoutInitialized_ = true;
    }

    // Main menu bar (generated from the panel registry)
    renderMainMenuBar();

    GuiFrameContext ctx{camera, deltaTime, fps};

    // Floating-window first-use defaults: cascade from the viewport work area,
    // sized relative to the viewport (never hardcoded pixel literals)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float cascadeStep = ImGui::GetFontSize() * 2.0f;

    for (size_t i = 0; i < panels_.size(); ++i) {
        PanelDesc& panel = panels_[i];
        if (!panel.open) continue;

        const float offset = static_cast<float>(i) * cascadeStep;
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.03f + offset,
                   viewport->WorkPos.y + viewport->WorkSize.y * 0.05f + offset),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(viewport->WorkSize.x * 0.22f, viewport->WorkSize.y * 0.40f),
            ImGuiCond_FirstUseEver);

        if (ImGui::Begin(panel.title.c_str(), &panel.open)) {
            panel.draw(ctx);
        }
        ImGui::End();
    }

    // Independent dockable Hierarchy and Inspector panels (special-cased:
    // menu bar in Hierarchy and shared selection state don't fit the loop).
    // ISceneControl is fetched per frame: the ECS world it exposes binds late.
    ISceneControl& sceneControl = systems_->sceneControl();
    if (showHierarchy_) {
        if (ImGui::Begin("Hierarchy", &showHierarchy_, ImGuiWindowFlags_MenuBar)) {
            GuiHierarchyPanel::renderCreateMenuBar(sceneControl, sceneEditorState);
            GuiHierarchyPanel::render(sceneControl, sceneEditorState);
        }
        ImGui::End();
    }
    if (showInspector_) {
        if (ImGui::Begin("Inspector", &showInspector_)) {
            GuiInspectorPanel::render(sceneControl, sceneEditorState);
        }
        ImGui::End();
    }

    // Transform gizmo: exactly once per frame while editor panels are in use
    if (showHierarchy_ || showInspector_) {
        GuiGizmo::render(camera, sceneControl, sceneEditorState);
    }

    // Skeleton/IK debug overlay
    const IKDebugSettings& ikSettings = ikTab_->settings();
    if (ikSettings.showSkeleton || ikSettings.showIKTargets) {
        ikTab_->drawSkeletonOverlay(camera, playerTab_->settings().showCapeColliders);
    }

    // Motion matching debug overlay
    const PlayerSettings& playerSettings = playerTab_->settings();
    if (playerSettings.motionMatchingEnabled &&
        (playerSettings.showMotionMatchingTrajectory ||
         playerSettings.showMotionMatchingFeatures ||
         playerSettings.showMotionMatchingStats)) {
        playerTab_->drawMotionMatchingOverlay(camera);
    }
}

void GuiSystem::endFrame(vk::CommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void GuiSystem::cancelFrame() {
    // End the ImGui frame without rendering to GPU
    // This must be called if beginFrame() was called but render won't happen
    ImGui::EndFrame();
}

bool GuiSystem::wantsInput() const {
    // The pause menu suppresses all gameplay input while open, even when
    // ImGui's capture flags briefly read false (e.g. cursor over the dim).
    if (gameMenu_.isOpen()) return true;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void GuiSystem::renderMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    for (MenuCategory category : kMenuOrder) {
        if (!ImGui::BeginMenu(menuCategoryName(category))) continue;

        // Special-cased editor panels live at the top of the Scene menu
        if (category == MenuCategory::Scene) {
            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
            ImGui::MenuItem("Inspector", nullptr, &showInspector_);
            ImGui::Separator();
        }

        for (PanelDesc& panel : panels_) {
            if (panel.category == category) {
                ImGui::MenuItem(panel.title.c_str(), nullptr, &panel.open);
            }
        }

        // View -> Windows: every panel in one list
        if (category == MenuCategory::View) {
            ImGui::Separator();
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
                ImGui::MenuItem("Inspector", nullptr, &showInspector_);
                for (PanelDesc& panel : panels_) {
                    ImGui::MenuItem(panel.title.c_str(), nullptr, &panel.open);
                }
                ImGui::EndMenu();
            }
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

bool GuiSystem::isGizmoActive() const {
    if (!visible) return false;
    if (!showHierarchy_ && !showInspector_) return false;
    return GuiGizmo::isUsing() || GuiGizmo::isOver();
}
