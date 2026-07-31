//
// Includes
//

// SDL
#include <SDL3/SDL.h>

// ImGui
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

// Uni.GUI
#include "ui_renderer_sdlgpu.h"
#include "SDL3/SDL_gpu.h"

namespace Uni::GUI {

    UiRendererSdlGpu::~UiRendererSdlGpu()
    {
        ShutdownImgui();

        if (m_gpu_device)
        {
            if (!SDL_WaitForGPUIdle(m_gpu_device)) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_WaitForGPUIdle(): %s", SDL_GetError());
            }

            if (m_ptr_window)
            {
                SDL_ReleaseWindowFromGPUDevice(
                    m_gpu_device,
                    static_cast<SDL_Window*>(m_ptr_window));
            }

            SDL_DestroyGPUDevice(m_gpu_device);
            m_gpu_device = nullptr;
        }

        m_ptr_window = nullptr;
    }

    bool UiRendererSdlGpu::Init(void* window_handle)
    {
        m_ptr_window = window_handle;
        SDL_Window* window = static_cast<SDL_Window*>(m_ptr_window);
        if (!window)
            return false;

        const SDL_PropertiesID create_properties = SDL_CreateProperties();
        if (create_properties == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_CreateProperties(): %s", SDL_GetError());
            return false;
        }
#if defined(NDEBUG)
        constexpr bool debug_mode = false;
#else
        constexpr bool debug_mode = true;
#endif
        const bool properties_configured =
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debug_mode) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, debug_mode) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, true) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, true) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, false) &&
            SDL_SetBooleanProperty(create_properties, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);
        if (properties_configured) {
            m_gpu_device = SDL_CreateGPUDeviceWithProperties(create_properties);
            if (!m_gpu_device) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_CreateGPUDeviceWithProperties(): %s", SDL_GetError());
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to configure SDL_GPU device properties: %s", SDL_GetError());
        }
        SDL_DestroyProperties(create_properties);
        if (!m_gpu_device) {
            return false;
        }

        const SDL_PropertiesID device_properties = SDL_GetGPUDeviceProperties(m_gpu_device);
        if (device_properties != 0) {
            const char* const backend = SDL_GetGPUDeviceDriver(m_gpu_device);
            const char* const device = SDL_GetStringProperty(
                device_properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown");
            const char* const driver = SDL_GetStringProperty(
                device_properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown");
            const char* driver_info = SDL_GetStringProperty(
                device_properties, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, nullptr);
            if (!driver_info) {
                driver_info = SDL_GetStringProperty(
                    device_properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "unknown");
            }
            SDL_Log("SDL_GPU backend=%s device=%s driver=%s version=%s",
                    backend ? backend : "unknown", device, driver, driver_info);
        }

        if (!SDL_ClaimWindowForGPUDevice(m_gpu_device, window))
        {
            SDL_DestroyGPUDevice(m_gpu_device);
            m_gpu_device = nullptr;
            return false;
        }

        SDL_GPUPresentMode present_mode =
            (m_vsync_interval != 0) ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

        if (!SDL_SetGPUSwapchainParameters(
                m_gpu_device,
                window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                present_mode)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_SetGPUSwapchainParameters(): %s", SDL_GetError());
            SDL_ReleaseWindowFromGPUDevice(m_gpu_device, window);
            SDL_DestroyGPUDevice(m_gpu_device);
            m_gpu_device = nullptr;
            m_ptr_window = nullptr;
            return false;
        }

        return true;
    }

    bool UiRendererSdlGpu::InitImgui()
    {
        SDL_Window* window = static_cast<SDL_Window*>(m_ptr_window);
        if (!window || !m_gpu_device)
            return false;

        if (ImGui::GetCurrentContext() == nullptr) {
            return false;
        }

        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            return false;
        }
        m_imgui_platform_initialized = true;

        // Renderer backend for SDL_GPU
        ImGui_ImplSDLGPU3_InitInfo init_info = {};
        init_info.Device = m_gpu_device;
        init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_gpu_device, window);
        init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
        init_info.PresentMode =
            (m_vsync_interval != 0) ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

        if (init_info.ColorTargetFormat == SDL_GPU_TEXTUREFORMAT_INVALID ||
            !ImGui_ImplSDLGPU3_Init(&init_info)) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_platform_initialized = false;
            return false;
        }
        m_imgui_renderer_initialized = true;

        return true;
    }

    void UiRendererSdlGpu::ShutdownImgui()
    {
        if (ImGui::GetCurrentContext() == nullptr) {
            return;
        }

        if ((m_imgui_platform_initialized || m_imgui_renderer_initialized) &&
            m_gpu_device && !SDL_WaitForGPUIdle(m_gpu_device)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_WaitForGPUIdle(): %s", SDL_GetError());
        }

        // Match the upstream SDL_GPU example: platform windows first, renderer resources second.
        if (m_imgui_platform_initialized) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_platform_initialized = false;
        }
        if (m_imgui_renderer_initialized) {
            ImGui_ImplSDLGPU3_Shutdown();
            m_imgui_renderer_initialized = false;
        }
    }

    void UiRendererSdlGpu::NewFrame()
    {
        // Order must mirror example_sdl3_sdlgpu3:
        // ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        ImGui_ImplSDLGPU3_NewFrame();
    }

    bool UiRendererSdlGpu::Render()
    {
        if (!m_gpu_device || !m_ptr_window)
            return false;

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (!draw_data)
            return false;

        if (draw_data->Textures) {
            for (ImTextureData* texture : *draw_data->Textures) {
                if (texture->Status != ImTextureStatus_OK) {
                    ImGui_ImplSDLGPU3_UpdateTexture(texture);
                }
            }
        }

        const bool is_minimized =
            (draw_data->DisplaySize.x <= 0.0f) ||
            (draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(m_gpu_device);
        if (!command_buffer) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_AcquireGPUCommandBuffer(): %s", SDL_GetError());
            return false;
        }

        SDL_GPUTexture* swapchain_texture = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                command_buffer,
                static_cast<SDL_Window*>(m_ptr_window),
                &swapchain_texture,
                nullptr,
                nullptr)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_WaitAndAcquireGPUSwapchainTexture(): %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(command_buffer);
            return false;
        }

        bool render_success = true;

        if (swapchain_texture != nullptr && !is_minimized)
        {
            // Mandatory: upload ImGui vertex/index buffers to the GPU for this frame.
            ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

            SDL_GPUColorTargetInfo target_info = {};
            target_info.texture = swapchain_texture;
            target_info.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
            target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            target_info.store_op = SDL_GPU_STOREOP_STORE;
            target_info.mip_level = 0;
            target_info.layer_or_depth_plane = 0;
            target_info.cycle = false;

            SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
                command_buffer,
                &target_info,
                1,
                nullptr
            );

            if (render_pass != nullptr)
            {
                ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
                SDL_EndGPURenderPass(render_pass);
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_BeginGPURenderPass(): %s", SDL_GetError());
                render_success = false;
            }
        }

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_SubmitGPUCommandBuffer(): %s", SDL_GetError());
            return false;
        }
        return render_success;
    }

    bool UiRendererSdlGpu::SetVsync(int interval)
    {
        if (!m_gpu_device || !m_ptr_window)
            return false;

        SDL_Window* window = static_cast<SDL_Window*>(m_ptr_window);

        SDL_GPUPresentMode present_mode =
            (interval != 0) ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

        if (!SDL_SetGPUSwapchainParameters(
                m_gpu_device,
                window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                present_mode))
        {
            return false;
        }

        m_vsync_interval = interval;
        return true;
    }

    std::string_view UiRendererSdlGpu::GetApiName() const {
        const char* name = m_gpu_device ? SDL_GetGPUDeviceDriver(m_gpu_device) : nullptr;
        return name ? std::string_view{name} : std::string_view{};
    }

} // namespace Uni::GUI
