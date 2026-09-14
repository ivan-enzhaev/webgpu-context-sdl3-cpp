#pragma once

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

#if defined(SDL_PLATFORM_WIN32)
#include <windows.h>
#elif defined(SDL_PLATFORM_ANDROID)
#include <android/log.h>
#include <android/native_window.h>
#endif

struct WebGPUContext
{
    SDL_Window *window = nullptr;
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUSurfaceConfiguration config = {};
    bool is_configured = false;
    bool need_reconfigure = false;
    bool need_recreate_surface = false;
};

extern WebGPUContext g_gpu;

void LogApp(const char *fmt, ...);

inline WGPUStringView WGPU_STR(const char *s)
{
    WGPUStringView view {};
    view.data = s;
    view.length = (s != nullptr) ? SDL_strlen(s) : WGPU_STRLEN;
    return view;
}

bool InitWebGPUContext(WebGPUContext *gpu, SDL_Window *win);
void ReconfigureSurfaceIfNeeded(WebGPUContext *gpu);
void DestroyWebGPUContext(WebGPUContext *gpu);
