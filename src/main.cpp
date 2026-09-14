#define SDL_MAIN_USE_CALLBACKS 1

#include "webgpu_context.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <concepts>

namespace
{
SDL_Window *g_window = nullptr;

#if defined(__ANDROID__) || defined(SDL_PLATFORM_ANDROID)
constexpr int WIN_WIDTH = 1280;
constexpr int WIN_HEIGHT = 720;
#else
constexpr int WIN_WIDTH = 300;
constexpr int WIN_HEIGHT = 300;
#endif

// C++20 Concept to type-check WebGPU surface status evaluation
template <typename T>
concept SurfaceStatus = requires(T s) { static_cast<int>(s); };

constexpr bool IsSurfaceStatusSuccess(SurfaceStatus auto status) noexcept
{
#if defined(WGPUSurfaceGetCurrentTextureStatus_Success) && defined(WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    return status == WGPUSurfaceGetCurrentTextureStatus_Success ||
           status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
#elif defined(WGPUSurfaceGetCurrentTextureStatus_SuccessStatus)
    return status == WGPUSurfaceGetCurrentTextureStatus_SuccessStatus;
#else
    return static_cast<int>(status) == 0 || static_cast<int>(status) == 1;
#endif
}
} // namespace

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
#ifndef __EMSCRIPTEN__
    if (!SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60"))
    {
        SDL_Log("Failed to set a frame rate: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return SDL_APP_FAILURE;
    }

    g_window = SDL_CreateWindow("WebGPU Context Setup", WIN_WIDTH, WIN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window)
    {
        return SDL_APP_FAILURE;
    }

    if (!InitWebGPUContext(&g_gpu, g_window))
    {
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        g_gpu.need_reconfigure = true;
    }

    // Capture Android native surface lifecycle
    if (event->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
    {
        g_gpu.need_recreate_surface = true;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (g_gpu.instance)
    {
        wgpuInstanceProcessEvents(g_gpu.instance);
    }

    if (!g_gpu.device)
    {
        return SDL_APP_CONTINUE;
    }

    ReconfigureSurfaceIfNeeded(&g_gpu);

    WGPUSurfaceTexture surfaceTexture {};
    wgpuSurfaceGetCurrentTexture(g_gpu.surface, &surfaceTexture);

    if (!IsSurfaceStatusSuccess(surfaceTexture.status))
    {
        static bool status_logged = false;
        if (!status_logged)
        {
            LogApp(">>> wgpuSurfaceGetCurrentTexture failed with status: %d",
                static_cast<int>(surfaceTexture.status));
            status_logged = true;
        }
        return SDL_APP_CONTINUE;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g_gpu.device, nullptr);

    // Modern C++20 Designated Initializers
    WGPURenderPassColorAttachment colorAttachment {
        .nextInChain = nullptr,
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .resolveTarget = nullptr,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = WGPUColor { 0.118, 0.220, 0.170, 1.0 }
    };

    WGPURenderPassDescriptor renderPassDesc {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(g_gpu.queue, 1, &commandBuffer);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g_gpu.surface);
#endif

    if (commandBuffer)
        wgpuCommandBufferRelease(commandBuffer);
    if (pass)
        wgpuRenderPassEncoderRelease(pass);
    if (encoder)
        wgpuCommandEncoderRelease(encoder);
    if (view)
        wgpuTextureViewRelease(view);
    if (surfaceTexture.texture)
        wgpuTextureRelease(surfaceTexture.texture);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    DestroyWebGPUContext(&g_gpu);
    if (g_window)
    {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}
