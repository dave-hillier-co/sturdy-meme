#include "GuiSystem.h"
#include "GuiInterfaces.h"
#include "Camera.h"

// Interface headers
#include "core/interfaces/ITimeSystem.h"
#include "core/interfaces/ILocationControl.h"
#include "core/interfaces/IWeatherState.h"
#include "core/interfaces/IEnvironmentControl.h"
#include "core/interfaces/IPostProcessState.h"
#include "core/interfaces/ICloudShadowControl.h"
#include "core/interfaces/ITerrainControl.h"
#include "core/interfaces/IWaterControl.h"
#include "core/interfaces/ITreeControl.h"
#include "core/interfaces/IDebugControl.h"
#include "core/interfaces/IProfilerControl.h"
#include "core/interfaces/IPerformanceControl.h"
#include "core/interfaces/ISceneControl.h"
#include "core/interfaces/IPlayerControl.h"
#include "EnvironmentSettings.h"

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
                                              vk::RenderPass renderPass, uint32_t imageCount) {
    auto gui = std::make_unique<GuiSystem>(ConstructToken{});
    if (!gui->initInternal(window, instance, physicalDevice, device, graphicsQueueFamily,
                           graphicsQueue, renderPass, imageCount)) {
        return nullptr;
    }
    return gui;
}

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

    buildPanelRegistry();

    SDL_Log("ImGui initialized successfully");
    return true;
}

void GuiSystem::buildPanelRegistry() {
    panels_.clear();

    // View
    panels_.push_back({"Dashboard", MenuCategory::View,
        [this](const GuiFrameContext& ctx) {
            GuiDashboard::render(ctx.interfaces.terrain, ctx.interfaces.time, ctx.camera,
                                 ctx.deltaTime, ctx.fps, dashboardState);
        }, true});
    panels_.push_back({"Position", MenuCategory::View,
        [](const GuiFrameContext& ctx) {
            GuiPositionPanel::render(ctx.camera);
        }, true});

    // Environment
    panels_.push_back({"Time", MenuCategory::Environment,
        [](const GuiFrameContext& ctx) {
            GuiTimeTab::render(ctx.interfaces.time, ctx.interfaces.location);
        }});
    panels_.push_back({"Weather", MenuCategory::Environment,
        [](const GuiFrameContext& ctx) {
            GuiWeatherTab::render(ctx.interfaces.weather, ctx.interfaces.environmentSettings);
        }});
    panels_.push_back({"Atmosphere", MenuCategory::Environment,
        [this](const GuiFrameContext& ctx) {
            GuiEnvironmentTab::render(ctx.interfaces.environment, environmentTabState);
        }});

    // Rendering
    panels_.push_back({"Post FX", MenuCategory::Rendering,
        [](const GuiFrameContext& ctx) {
            GuiPostFXTab::render(ctx.interfaces.postProcess, ctx.interfaces.cloudShadow);
        }});
    panels_.push_back({"Terrain", MenuCategory::Rendering,
        [](const GuiFrameContext& ctx) {
            GuiTerrainTab::render(ctx.interfaces.terrain);
        }});
    panels_.push_back({"Water", MenuCategory::Rendering,
        [](const GuiFrameContext& ctx) {
            GuiWaterTab::render(ctx.interfaces.water);
        }});
    panels_.push_back({"Trees", MenuCategory::Rendering,
        [](const GuiFrameContext& ctx) {
            GuiTreeTab::render(ctx.interfaces.tree);
        }});
    panels_.push_back({"Grass", MenuCategory::Rendering,
        [](const GuiFrameContext& ctx) {
            GuiGrassTab::render(ctx.interfaces.grass);
        }});

    // Character
    panels_.push_back({"Player", MenuCategory::Character,
        [this](const GuiFrameContext& ctx) {
            GuiPlayerTab::render(ctx.interfaces.player, playerSettings);
        }});
    panels_.push_back({"NPC LOD", MenuCategory::Character,
        [](const GuiFrameContext& ctx) {
            GuiNPCTab::render(ctx.interfaces.player);
        }});
    panels_.push_back({"IK / Animation", MenuCategory::Character,
        [this](const GuiFrameContext& ctx) {
            GuiIKTab::render(ctx.interfaces.scene, ctx.camera, ikDebugSettings);
        }});

    // Scene: Hierarchy and Inspector are special-cased outside the registry.
    // The former "Scene Graph" panel is now the "Renderables" tab inside Hierarchy.

    // Debug
    panels_.push_back({"Debug Visualizations", MenuCategory::Debug,
        [](const GuiFrameContext& ctx) {
            GuiDebugTab::render(ctx.interfaces.debug, ctx.interfaces.debugCommands);
        }});
    panels_.push_back({"Performance Toggles", MenuCategory::Debug,
        [](const GuiFrameContext& ctx) {
            GuiPerformanceTab::render(ctx.interfaces.performance);
        }});
    panels_.push_back({"Profiler", MenuCategory::Debug,
        [](const GuiFrameContext& ctx) {
            GuiProfilerTab::render(ctx.interfaces.profiler);
        }});
    panels_.push_back({"Tile Loader", MenuCategory::Debug,
        [this](const GuiFrameContext& ctx) {
            GuiTileLoaderTab::render(ctx.interfaces.terrain, ctx.interfaces.physicsTerrainTiles,
                                     ctx.camera, tileLoaderState);
        }});
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

void GuiSystem::render(GuiInterfaces& ui, const Camera& camera, float deltaTime, float fps) {
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

    GuiFrameContext ctx{ui, camera, deltaTime, fps};

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
    // menu bar in Hierarchy and shared selection state don't fit the loop)
    if (showHierarchy_) {
        if (ImGui::Begin("Hierarchy", &showHierarchy_, ImGuiWindowFlags_MenuBar)) {
            GuiHierarchyPanel::renderCreateMenuBar(ui.scene, sceneEditorState);
            GuiHierarchyPanel::render(ui.scene, sceneEditorState);
        }
        ImGui::End();
    }
    if (showInspector_) {
        if (ImGui::Begin("Inspector", &showInspector_)) {
            GuiInspectorPanel::render(ui.scene, sceneEditorState);
        }
        ImGui::End();
    }

    // Transform gizmo: exactly once per frame while editor panels are in use
    if (showHierarchy_ || showInspector_) {
        GuiGizmo::render(camera, ui.scene, sceneEditorState);
    }

    // Skeleton/IK debug overlay
    if (ikDebugSettings.showSkeleton || ikDebugSettings.showIKTargets) {
        GuiIKTab::renderSkeletonOverlay(ui.scene, camera, ikDebugSettings, playerSettings.showCapeColliders);
    }

    // Motion matching debug overlay
    if (playerSettings.motionMatchingEnabled &&
        (playerSettings.showMotionMatchingTrajectory ||
         playerSettings.showMotionMatchingFeatures ||
         playerSettings.showMotionMatchingStats)) {
        GuiPlayerTab::renderMotionMatchingOverlay(ui.player, camera, playerSettings);
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
