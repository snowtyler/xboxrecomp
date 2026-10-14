#include "fb_gpu_present.h"
#include <stdlib.h>

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

#define RELEASE(object, type) do { \
    if (object) { type##_Release(object); object = NULL; } \
} while (0)

static struct {
    IDXGISwapChain1 *chain;
    ID3D11Texture2D *buffer;
    ID3D11Device *device; /* Borrowed; swap chain owns a device reference. */
    ID3D11DeviceContext *context; /* Owned until deferred chain destruction is flushed. */
    HWND window;
    uint32_t width, height;
    int prepared;
    unsigned errors;
    int profile_initialized, profile;
    uint64_t frames, gpu_copies, cpu_uploads, copy_ticks, present_ticks;
    LARGE_INTEGER frequency;
} s_presenter;

int nv2a_gpu_present_enabled(void)
{
    return getenv("RECOMP_HW_PRESENT") != NULL && getenv("RECOMP_FB_VA") == NULL;
}

static int failed(const char *operation, HRESULT hr)
{
    if (s_presenter.errors++ < 8)
        fprintf(stderr, "[GPU PRESENT] %s failed %08lX\n", operation, (unsigned long)hr);
    s_presenter.prepared = 0;
    return 0;
}

void nv2a_gpu_present_shutdown(void)
{
    int had_chain = s_presenter.chain != NULL;
    RELEASE(s_presenter.buffer, ID3D11Texture2D);
    RELEASE(s_presenter.chain, IDXGISwapChain1);
    /* A replacement flip chain on the same HWND requires deferred destruction
     * of the old chain to be forced. No presenter views are bound to context
     * state, so ClearState would needlessly disturb the renderer's bindings.
     * Flush releases deferred objects; it is not a GPU-completion signal. */
    if (had_chain && s_presenter.context)
        ID3D11DeviceContext_Flush(s_presenter.context);
    RELEASE(s_presenter.context, ID3D11DeviceContext);
    s_presenter.device = NULL;
    s_presenter.window = NULL;
    s_presenter.width = s_presenter.height = 0;
    s_presenter.prepared = 0;
}

static int create_chain(ID3D11Device *device, ID3D11DeviceContext *context, HWND window,
                        uint32_t width, uint32_t height)
{
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc;
    HRESULT hr;
    /* Acquire before the first creation attempt so every partial-failure
     * rollback can flush a returned chain and then balance this reference. */
    ID3D11DeviceContext_AddRef(context);
    s_presenter.context = context;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    if (SUCCEEDED(hr)) hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IDXGIFactory2_CreateSwapChainForHwnd(factory,
        (IUnknown *)device, window, &desc, NULL, NULL, &s_presenter.chain);
    if (SUCCEEDED(hr)) hr = IDXGIFactory2_MakeWindowAssociation(factory, window,
        DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    if (SUCCEEDED(hr)) hr = IDXGISwapChain1_GetBuffer(s_presenter.chain, 0,
        &IID_ID3D11Texture2D, (void **)&s_presenter.buffer);
    RELEASE(factory, IDXGIFactory2);
    RELEASE(adapter, IDXGIAdapter);
    RELEASE(dxgi_device, IDXGIDevice);
    if (FAILED(hr)) {
        nv2a_gpu_present_shutdown();
        return failed("create swap chain", hr);
    }
    s_presenter.device = device;
    s_presenter.window = window;
    s_presenter.width = width;
    s_presenter.height = height;
    fprintf(stderr, "[GPU PRESENT] D3D11 flip swap chain ready %ux%u\n", width, height);
    return 1;
}

int nv2a_gpu_present_prepare(void *device_ptr, void *context_ptr, void *texture_ptr,
                             const void *bgra, uint32_t pitch,
                             uint32_t width, uint32_t height, void *window_ptr)
{
    ID3D11Device *device = device_ptr;
    ID3D11DeviceContext *context = context_ptr;
    ID3D11Texture2D *texture = texture_ptr;
    HWND window = window_ptr;
    HRESULT hr;
    LARGE_INTEGER began = {0}, ended;
    s_presenter.prepared = 0;
    if (!device || !context || !window || !IsWindow(window) || !width || !height)
        return 0;
    if (ID3D11DeviceContext_GetType(context) != D3D11_DEVICE_CONTEXT_IMMEDIATE)
        return 0;
    if (texture) {
        D3D11_TEXTURE2D_DESC source;
        ID3D11Texture2D_GetDesc(texture, &source);
        if (source.Width != width || source.Height != height || source.MipLevels != 1 ||
            source.ArraySize != 1 || source.SampleDesc.Count != 1 ||
            source.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            return 0;
    } else if (!bgra || width > UINT32_MAX / 4 || pitch < width * 4) {
        return 0;
    }
    if (s_presenter.chain && (s_presenter.device != device ||
        s_presenter.context != context || s_presenter.window != window))
        nv2a_gpu_present_shutdown();
    if (!s_presenter.chain && !create_chain(device, context, window, width, height))
        return 0;
    if (s_presenter.width != width || s_presenter.height != height) {
        RELEASE(s_presenter.buffer, ID3D11Texture2D);
        hr = IDXGISwapChain1_ResizeBuffers(s_presenter.chain, 2, width, height,
            DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (SUCCEEDED(hr)) hr = IDXGISwapChain1_GetBuffer(s_presenter.chain, 0,
            &IID_ID3D11Texture2D, (void **)&s_presenter.buffer);
        if (FAILED(hr)) {
            nv2a_gpu_present_shutdown();
            return failed("resize buffers", hr);
        }
        s_presenter.width = width;
        s_presenter.height = height;
    }
    if (!s_presenter.profile_initialized) {
        s_presenter.profile_initialized = 1;
        s_presenter.profile = getenv("RECOMP_GPU_TIMING") != NULL;
        QueryPerformanceFrequency(&s_presenter.frequency);
    }
    if (s_presenter.profile) QueryPerformanceCounter(&began);
    if (texture) {
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)s_presenter.buffer,
            (ID3D11Resource *)texture);
        ++s_presenter.gpu_copies;
    } else {
        ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)s_presenter.buffer,
            0, NULL, bgra, pitch, 0);
        ++s_presenter.cpu_uploads;
    }
    if (s_presenter.profile) {
        QueryPerformanceCounter(&ended);
        s_presenter.copy_ticks += ended.QuadPart - began.QuadPart;
    }
    s_presenter.prepared = 1;
    return 1;
}

void *nv2a_gpu_present_backbuffer(void)
{
    return s_presenter.prepared ? s_presenter.buffer : NULL;
}

int nv2a_gpu_present_commit(void)
{
    HRESULT hr;
    LARGE_INTEGER began = {0}, ended;
    if (!s_presenter.prepared || !s_presenter.chain)
        return 0;
    s_presenter.prepared = 0;
    /* The guest has its own vblank/flip ordering. Host presentation adds no
     * simulated guest completion and never alters the guest semaphore state. */
    if (s_presenter.profile) QueryPerformanceCounter(&began);
    hr = IDXGISwapChain1_Present(s_presenter.chain, 0, 0);
    if (FAILED(hr)) {
        failed("Present", hr);
        nv2a_gpu_present_shutdown();
        return 0;
    }
    ++s_presenter.frames;
    if (s_presenter.profile) {
        QueryPerformanceCounter(&ended);
        s_presenter.present_ticks += ended.QuadPart - began.QuadPart;
        if (s_presenter.frames % 500 == 0) {
            fprintf(stderr, "[GPU PRESENT] frames=%llu gpu_copies=%llu cpu_uploads=%llu copy %.2f ms/frame present %.2f ms/frame\n",
                (unsigned long long)s_presenter.frames, (unsigned long long)s_presenter.gpu_copies,
                (unsigned long long)s_presenter.cpu_uploads,
                s_presenter.copy_ticks * 1000.0 / s_presenter.frequency.QuadPart / 500,
                s_presenter.present_ticks * 1000.0 / s_presenter.frequency.QuadPart / 500);
            s_presenter.copy_ticks = s_presenter.present_ticks = 0;
        }
    }
    return 1;
}

int nv2a_gpu_present(void *device, void *context, void *texture,
                     const void *bgra, uint32_t pitch,
                     uint32_t width, uint32_t height, void *window)
{
    return nv2a_gpu_present_prepare(device, context, texture, bgra, pitch,
                                   width, height, window) && nv2a_gpu_present_commit();
}

#else
int nv2a_gpu_present_enabled(void) { return 0; }
int nv2a_gpu_present_prepare(void *device, void *context, void *texture,
                             const void *bgra, uint32_t pitch,
                             uint32_t width, uint32_t height, void *window)
{
    (void)device; (void)context; (void)texture; (void)bgra;
    (void)pitch; (void)width; (void)height; (void)window;
    return 0;
}
void *nv2a_gpu_present_backbuffer(void) { return NULL; }
int nv2a_gpu_present_commit(void) { return 0; }
int nv2a_gpu_present(void *device, void *context, void *texture,
                     const void *bgra, uint32_t pitch,
                     uint32_t width, uint32_t height, void *window)
{
    return nv2a_gpu_present_prepare(device, context, texture, bgra, pitch, width, height, window);
}
void nv2a_gpu_present_shutdown(void) {}
#endif
