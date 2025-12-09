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

namespace Uni::GUI {

    UiRendererSdlGpu::~UiRendererSdlGpu()
    {
        // Shutdown ImGui GPU backend (safe to call if not initialized)
        ImGui_ImplSDLGPU3_Shutdown();

        if (m_gpu_device)
        {
            SDL_WaitForGPUIdle(m_gpu_device);

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

        const Uint32 shader_formats =
            SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_DXIL |
            SDL_GPU_SHADERFORMAT_MSL |
            SDL_GPU_SHADERFORMAT_METALLIB;

        m_gpu_device = SDL_CreateGPUDevice(shader_formats, true, nullptr);
        if (!m_gpu_device)
            return false;

        if (!SDL_ClaimWindowForGPUDevice(m_gpu_device, window))
        {
            SDL_DestroyGPUDevice(m_gpu_device);
            m_gpu_device = nullptr;
            return false;
        }

        SDL_GPUPresentMode present_mode =
            (m_vsync_interval != 0) ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

        SDL_SetGPUSwapchainParameters(
            m_gpu_device,
            window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            present_mode
        );

        return true;
    }

    bool UiRendererSdlGpu::InitImgui()
    {
        SDL_Window* window = static_cast<SDL_Window*>(m_ptr_window);
        if (!window || !m_gpu_device)
            return false;

        // Platform backend for SDL3 + SDL_GPU
        ImGui_ImplSDL3_InitForSDLGPU(window);

        // Renderer backend for SDL_GPU
        ImGui_ImplSDLGPU3_InitInfo init_info = {};
        init_info.Device = m_gpu_device;
        init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_gpu_device, window);
        init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
        init_info.PresentMode =
            (m_vsync_interval != 0) ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;

        ImGui_ImplSDLGPU3_Init(&init_info);

        return true;
    }

    void UiRendererSdlGpu::NewFrame(std::pair<size_t, size_t> /*new_size*/)
    {
        // Order must mirror example_sdl3_sdlgpu3:
        // ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        ImGui_ImplSDLGPU3_NewFrame();
    }

    void UiRendererSdlGpu::Render()
    {
        if (!m_gpu_device || !m_ptr_window)
            return;

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (!draw_data)
            return;

        const bool is_minimized =
            (draw_data->DisplaySize.x <= 0.0f) ||
            (draw_data->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(m_gpu_device);
        if (!command_buffer)
            return;

        SDL_GPUTexture* swapchain_texture = nullptr;
        SDL_WaitAndAcquireGPUSwapchainTexture(
            command_buffer,
            static_cast<SDL_Window*>(m_ptr_window),
            &swapchain_texture,
            nullptr,
            nullptr
        );

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
            }
        }

        SDL_SubmitGPUCommandBuffer(command_buffer);
    }

    bool UiRendererSdlGpu::SetVsync(int interval)
    {
        m_vsync_interval = interval;

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

        return true;
    }

} // namespace Uni::GUI
