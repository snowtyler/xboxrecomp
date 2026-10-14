#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fb_gpu_present.h"

extern void xbox_FramebufferWindowStart(void);
extern int xbox_FramebufferKeyDown(int vk);
ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }

static HWND s_window;
static unsigned s_failures, s_pixels;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition); \
    ++s_failures; goto cleanup; \
} } while (0)
#define RELEASE(object, type) do { \
    if (object) { type##_Release(object); object = NULL; } \
} while (0)

static uint32_t pixel(unsigned x, unsigned y, unsigned seed)
{
    return ((x * 11 + y * 3 + seed) & 255) |
           (((x * 7 + y * 13 + seed * 5) & 255) << 8) |
           (((x * 17 + y * 19 + seed * 7) & 255) << 16) |
           (((x * 23 + y * 29 + seed * 31) & 255) << 24);
}

static void fill(unsigned char *data, unsigned pitch, unsigned width,
                 unsigned height, unsigned seed)
{
    memset(data, 0xA5, (size_t)pitch * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            ((uint32_t *)(data + (size_t)y * pitch))[x] = pixel(x, y, seed);
}

/* Read the actual swap-chain backbuffer before Present can rotate it. The
 * blocking map here verifies completion of the real copy and every BGRA byte;
 * this fixture's readback is never part of the gameplay presentation path. */
static int verify(ID3D11Device *device, ID3D11DeviceContext *context,
                   const unsigned char *expected, unsigned pitch,
                   unsigned width, unsigned height)
{
    ID3D11Texture2D *buffer = nv2a_gpu_present_backbuffer(), *staging = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr;
    int okay = 1;
    if (!buffer)
        return 0;
    ID3D11Texture2D_GetDesc(buffer, &desc);
    if (desc.Width != width || desc.Height != height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr))
        return 0;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)buffer);
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (SUCCEEDED(hr)) {
        for (unsigned y = 0; y < height; ++y)
            if (memcmp((const unsigned char *)map.pData + (size_t)y * map.RowPitch,
                       expected + (size_t)y * pitch, (size_t)width * 4)) {
                okay = 0;
                fprintf(stderr, "Backbuffer byte mismatch row %u\n", y);
                break;
            }
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    } else {
        okay = 0;
    }
    RELEASE(staging, ID3D11Texture2D);
    if (okay)
        s_pixels += width * height;
    return okay;
}

int main(void)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *source = NULL;
    ID3D11Texture2D *held_buffer = NULL;
    D3D_FEATURE_LEVEL feature;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SUBRESOURCE_DATA upload;
    unsigned width = 64, height = 32, pitch = 64 * 4 + 32;
    unsigned char *pixels = NULL;
    DWORD owner;
    HRESULT hr;
    _putenv_s("RECOMP_FB_WINDOW", "1");
    _putenv_s("RECOMP_HW_PRESENT", "1");
    _putenv_s("RECOMP_FB_VA", "");
    CHECK(nv2a_gpu_present_enabled());
    xbox_FramebufferWindowStart();
    for (unsigned attempt = 0; attempt < 200 && !s_window; ++attempt) {
        s_window = xbox_FramebufferWindowHandle();
        if (!s_window) Sleep(10);
    }
    CHECK(s_window && IsWindowVisible(s_window));
    GetWindowThreadProcessId(s_window, &owner);
    CHECK(owner == GetCurrentProcessId());
    CHECK(!xbox_FramebufferWindowGpuActive());
    for (unsigned attempt = 0; attempt < 200 && !xbox_FramebufferWindowGpuRequest(1); ++attempt)
        Sleep(10);
    CHECK(xbox_FramebufferWindowGpuActive());
    CHECK(xbox_FramebufferWindowHandle() == s_window);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &device, &feature, &context);
    CHECK(SUCCEEDED(hr));
    pixels = malloc((size_t)pitch * height);
    CHECK(pixels);
    fill(pixels, pitch, width, height, 3);
    memset(&desc, 0, sizeof(desc));
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    memset(&upload, 0, sizeof(upload));
    upload.pSysMem = pixels; upload.SysMemPitch = pitch;
    CHECK(SUCCEEDED(ID3D11Device_CreateTexture2D(device, &desc, &upload, &source)));
    CHECK(nv2a_gpu_present_prepare(device, context, source, NULL, 0, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    /* Queue a copy, then recreate on the same HWND without a fixture map or
     * completion wait in between. Shutdown must flush deferred destruction. */
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    nv2a_gpu_present_shutdown();
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    /* Deliberately violate the borrowed-buffer lifetime to force a real
     * ResizeBuffers failure. The module must drop all of its own references,
     * and recover once this fixture releases the outstanding reference. */
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    held_buffer = nv2a_gpu_present_backbuffer();
    ID3D11Texture2D_AddRef(held_buffer);
    CHECK(!nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height - 1, s_window));
    CHECK(!nv2a_gpu_present_backbuffer());
    RELEASE(held_buffer, ID3D11Texture2D);
    ID3D11DeviceContext_Flush(context);
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    CHECK(!nv2a_gpu_present_backbuffer());
    CHECK(!nv2a_gpu_present_commit());
    /* CPU fallback uploads padded rows, preserving all four channels. */
    fill(pixels, pitch, width, height, 9);
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    CHECK(!nv2a_gpu_present_prepare(device, context, source, NULL, 0, width + 1, height, s_window));
    CHECK(!nv2a_gpu_present_prepare(device, context, NULL, pixels, width * 4 - 1, width, height, s_window));
    /* Window resize scales the same source; source resize recreates buffers. */
    CHECK(SetWindowPos(s_window, NULL, 0, 0, 820, 620, SWP_NOMOVE | SWP_NOZORDER));
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    free(pixels); pixels = NULL;
    width = 96; height = 48; pitch = width * 4 + 16;
    pixels = malloc((size_t)pitch * height);
    CHECK(pixels);
    fill(pixels, pitch, width, height, 17);
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    ShowWindow(s_window, SW_MINIMIZE);
    CHECK(nv2a_gpu_present(device, context, NULL, pixels, pitch, width, height, s_window));
    ShowWindow(s_window, SW_RESTORE);
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    /* GPU mode must retain the window's existing key/focus and close handling. */
    SendMessageA(s_window, WM_KEYDOWN, 'Z', 0);
    SendMessageA(s_window, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageA(s_window, WM_KEYDOWN, VK_NUMPAD8, 0);
    SendMessageA(s_window, WM_KEYDOWN, '3', 0);
    CHECK(xbox_FramebufferKeyDown('Z') && xbox_FramebufferKeyDown(VK_RETURN));
    CHECK(xbox_FramebufferKeyDown(VK_NUMPAD8) && xbox_FramebufferKeyDown('3'));
    SendMessageA(s_window, WM_KEYUP, 'Z', 0);
    CHECK(!xbox_FramebufferKeyDown('Z'));
    SendMessageA(s_window, WM_KILLFOCUS, 0, 0);
    CHECK(!xbox_FramebufferKeyDown(VK_RETURN));
    CHECK(!xbox_FramebufferKeyDown(VK_NUMPAD8) && !xbox_FramebufferKeyDown('3'));
    nv2a_gpu_present_shutdown();
    CHECK(xbox_FramebufferWindowGpuRequest(0));
    CHECK(!xbox_FramebufferWindowGpuActive());
    for (unsigned attempt = 0; attempt < 200 && !xbox_FramebufferWindowGpuRequest(1); ++attempt)
        Sleep(10);
    CHECK(xbox_FramebufferWindowGpuActive());
    CHECK(nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
    CHECK(verify(device, context, pixels, pitch, width, height));
    CHECK(nv2a_gpu_present_commit());
    SendMessageA(s_window, WM_CLOSE, 0, 0);
    for (unsigned attempt = 0; attempt < 200 && IsWindow(s_window); ++attempt) Sleep(10);
    CHECK(!IsWindow(s_window));
    CHECK(!xbox_FramebufferWindowHandle());
    CHECK(!nv2a_gpu_present_prepare(device, context, NULL, pixels, pitch, width, height, s_window));
cleanup:
    nv2a_gpu_present_shutdown();
    RELEASE(held_buffer, ID3D11Texture2D);
    if (context) ID3D11DeviceContext_Flush(context);
    xbox_FramebufferWindowGpuRequest(0);
    if (s_window && IsWindow(s_window)) {
        SendMessageA(s_window, WM_CLOSE, 0, 0);
        for (unsigned attempt = 0; attempt < 200 && IsWindow(s_window); ++attempt) Sleep(10);
    }
    RELEASE(source, ID3D11Texture2D);
    RELEASE(context, ID3D11DeviceContext);
    RELEASE(device, ID3D11Device);
    free(pixels);
    printf("Native swap-chain GPU/CPU byte copies (%u pixels), resize/minimize/restore, keyboard/focus/owned-close: %u failures\n",
        s_pixels, s_failures);
    return s_failures ? 1 : 0;
}
