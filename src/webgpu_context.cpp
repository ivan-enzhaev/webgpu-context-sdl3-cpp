#include "webgpu_context.hpp"

#include <cstdarg>
#include <cstdio>

WebGPUContext g_gpu {};

void LogApp(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

#if defined(__ANDROID__)
    __android_log_vprint(ANDROID_LOG_ERROR, "MY_APP", fmt, args);
#else
    std::printf("[MY_APP] ");
    std::vprintf(fmt, args);
    std::printf("\n");
#endif

    va_end(args);
}

WGPUSurface CreateWGPUSurface(WGPUInstance inst, SDL_Window *win)
{
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource {};
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPU_STR("#canvas");

    WGPUSurfaceDescriptor desc {};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&canvasSource);
    return wgpuInstanceCreateSurface(inst, &desc);

#elif defined(SDL_PLATFORM_WIN32)
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    auto hinstance = static_cast<HINSTANCE>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr));

    WGPUSurfaceSourceWindowsHWND hwndSource {};
    hwndSource.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwndSource.hinstance = hinstance;
    hwndSource.hwnd = hwnd;

    WGPUSurfaceDescriptor desc {};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&hwndSource);
    return wgpuInstanceCreateSurface(inst, &desc);

#elif defined(SDL_PLATFORM_ANDROID)
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    auto aNativeWindow = static_cast<ANativeWindow *>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));

    WGPUSurfaceSourceAndroidNativeWindow androidSource {};
    androidSource.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
    androidSource.window = aNativeWindow;

    WGPUSurfaceDescriptor desc {};
    desc.nextInChain = reinterpret_cast<WGPUChainedStruct *>(&androidSource);
    return wgpuInstanceCreateSurface(inst, &desc);

#elif defined(SDL_PLATFORM_LINUX)
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    
    // Check Wayland first
    void *wayland_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    void *wayland_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);

    if (wayland_display && wayland_surface)
    {
        WGPUSurfaceSourceWaylandSurface waylandSource = {
            .chain = { .sType = WGPUSType_SurfaceSourceWaylandSurface },
            .display = wayland_display,
            .surface = wayland_surface
        };
        WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&waylandSource };
        return wgpuInstanceCreateSurface(inst, &desc);
    }

    // Fall back to X11 (Xlib)
    void *x11_display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    uint64_t x11_window = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);

    if (x11_display && x11_window)
    {
        WGPUSurfaceSourceXlibWindow x11Source = {
            .chain = { .sType = WGPUSType_SurfaceSourceXlibWindow },
            .display = x11_display,
            .window = x11_window
        };
        WGPUSurfaceDescriptor desc = { .nextInChain = (WGPUChainedStruct *)&x11Source };
        return wgpuInstanceCreateSurface(inst, &desc);
    }

    return NULL;
#else
#error "Platform surface mapping not implemented"
#endif
}

void handle_device_error(
    WGPUDevice const *device,
    WGPUErrorType type,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "(No message)";
    int len = (message.data && message.length > 0) ? static_cast<int>(message.length) : 12;

    LogApp("WebGPU Error [%d]: %.*s", static_cast<int>(type), len, msg);
}

void handle_device_request(
    WGPURequestDeviceStatus status,
    WGPUDevice res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "";
    int len = (message.data && message.length > 0) ? static_cast<int>(message.length) : 0;

    LogApp("Device callback: status=%d device=%p message=%.*s", static_cast<int>(status), reinterpret_cast<void *>(res), len, msg);

    if (status == WGPURequestDeviceStatus_Success)
    {
        g_gpu.device = res;
        LogApp("DEVICE CREATED SUCCESS");
    }
    else
    {
        LogApp("DEVICE CREATION FAILED");
    }
}

void handle_adapter_request(
    WGPURequestAdapterStatus status,
    WGPUAdapter res,
    WGPUStringView message,
    void *userdata1,
    void *userdata2)
{
    const char *msg = (message.data && message.length > 0) ? message.data : "";
    int len = (message.data && message.length > 0) ? static_cast<int>(message.length) : 0;

    LogApp("Adapter callback: status=%d adapter=%p message=%.*s", static_cast<int>(status), reinterpret_cast<void *>(res), len, msg);

    if (status == WGPURequestAdapterStatus_Success)
    {
        g_gpu.adapter = res;
        LogApp("ADAPTER CREATED SUCCESS");

        WGPUUncapturedErrorCallbackInfo errorCallbackInfo {};
        errorCallbackInfo.callback = handle_device_error;

        WGPUDeviceDescriptor deviceDesc {};
        deviceDesc.uncapturedErrorCallbackInfo = errorCallbackInfo;

        WGPURequestDeviceCallbackInfo deviceCallbackInfo {};
        deviceCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceCallbackInfo.callback = handle_device_request;

        wgpuAdapterRequestDevice(g_gpu.adapter, &deviceDesc, deviceCallbackInfo);
    }
    else
    {
        LogApp("ADAPTER CREATION FAILED");
    }
}

bool InitWebGPUContext(WebGPUContext *gpu, SDL_Window *win)
{
    gpu->window = win;
    gpu->instance = wgpuCreateInstance(nullptr);
    if (!gpu->instance)
        return false;

    gpu->surface = CreateWGPUSurface(gpu->instance, win);
    if (!gpu->surface)
        return false;

    WGPURequestAdapterOptions opt {};
    opt.compatibleSurface = gpu->surface;

    WGPURequestAdapterCallbackInfo adapterCallbackInfo {};
    adapterCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallbackInfo.callback = reinterpret_cast<WGPURequestAdapterCallback>(handle_adapter_request);

    wgpuInstanceRequestAdapter(gpu->instance, &opt, adapterCallbackInfo);

#ifndef __EMSCRIPTEN__
    while (gpu->adapter == nullptr || gpu->device == nullptr)
    {
        SDL_Delay(1);
    }
#endif

    return true;
}

void ReconfigureSurfaceIfNeeded(WebGPUContext *gpu)
{
    if (!gpu->device)
        return;

    // Handle native window recreation on Android foreground return
    if (gpu->need_recreate_surface)
    {
        if (gpu->surface)
        {
            wgpuSurfaceUnconfigure(gpu->surface);
            wgpuSurfaceRelease(gpu->surface);
            gpu->surface = nullptr;
        }

        // Fetch new ANativeWindow pointer internally via SDL_GetPointerProperty
        gpu->surface = CreateWGPUSurface(gpu->instance, gpu->window);
        gpu->need_recreate_surface = false;

        // Capabilities and width/height must be rebound
        gpu->is_configured = false;
        gpu->need_reconfigure = true;
    }

    if (!gpu->surface)
        return;

    if (!gpu->is_configured || gpu->need_reconfigure)
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(gpu->window, &w, &h);
        if (w <= 0 || h <= 0)
            return;

        gpu->config.width = static_cast<uint32_t>(w);
        gpu->config.height = static_cast<uint32_t>(h);

        if (!gpu->is_configured)
        {
            if (!gpu->queue)
            {
                gpu->queue = wgpuDeviceGetQueue(gpu->device);
            }

            WGPUSurfaceCapabilities caps {};
            wgpuSurfaceGetCapabilities(gpu->surface, gpu->adapter, &caps);

            WGPUTextureFormat surface_format = WGPUTextureFormat_Undefined;
            if (caps.formatCount > 0)
            {
                surface_format = caps.formats[0];
                for (size_t i = 0; i < caps.formatCount; ++i)
                {
                    if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm ||
                        caps.formats[i] == WGPUTextureFormat_BGRA8Unorm)
                    {
                        surface_format = caps.formats[i];
                        break;
                    }
                }
            }
            else
            {
                surface_format = WGPUTextureFormat_RGBA8Unorm;
            }

            LogApp(">>> Selected Surface Format: %d", static_cast<int>(surface_format));

            WGPUPresentMode present_mode = (caps.presentModeCount > 0) ? caps.presentModes[0] : WGPUPresentMode_Fifo;

            gpu->config.device = gpu->device;
            gpu->config.format = surface_format;
            gpu->config.usage = WGPUTextureUsage_RenderAttachment;
            gpu->config.presentMode = present_mode;

            wgpuSurfaceCapabilitiesFreeMembers(caps);
            gpu->is_configured = true;
        }

        LogApp(">>> Configuring Surface: width=%u height=%u", gpu->config.width, gpu->config.height);
        wgpuSurfaceConfigure(gpu->surface, &gpu->config);
        gpu->need_reconfigure = false;
    }
}

void DestroyWebGPUContext(WebGPUContext *gpu)
{
    if (gpu->surface)
        wgpuSurfaceUnconfigure(gpu->surface);
    if (gpu->queue)
        wgpuQueueRelease(gpu->queue);
    if (gpu->device)
        wgpuDeviceRelease(gpu->device);
    if (gpu->adapter)
        wgpuAdapterRelease(gpu->adapter);
    if (gpu->surface)
        wgpuSurfaceRelease(gpu->surface);
    if (gpu->instance)
        wgpuInstanceRelease(gpu->instance);
}
