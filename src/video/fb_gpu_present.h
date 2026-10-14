#ifndef FB_GPU_PRESENT_H
#define FB_GPU_PRESENT_H

#include <stdint.h>

/* All presenter calls belong to the thread using the supplied immediate
 * context. Arguments are native ID3D11Device/Context/Texture2D and HWND
 * pointers on Windows; this header deliberately does not include COM headers.
 * A GPU source must be single-sample B8G8R8A8_UNORM with matching dimensions.
 * Otherwise supply completed CPU BGRA pixels (pitch >= width * 4).
 * The caller must obtain the window's GPU-mode acknowledgment first. The
 * presenter owns one immediate-context reference while a chain exists, and
 * releases/flushes the chain before balancing that reference on shutdown. */
int nv2a_gpu_present_enabled(void);
int nv2a_gpu_present_prepare(void *device, void *context, void *texture,
                             const void *bgra, uint32_t pitch,
                             uint32_t width, uint32_t height, void *window);
/* Borrowed backbuffer for same-thread verification between prepare/commit.
 * Do not bind its views or retain references past the next prepare/shutdown. */
void *nv2a_gpu_present_backbuffer(void);
int nv2a_gpu_present_commit(void);
int nv2a_gpu_present(void *device, void *context, void *texture,
                     const void *bgra, uint32_t pitch,
                     uint32_t width, uint32_t height, void *window);
void nv2a_gpu_present_shutdown(void);

/* Window/GDI transition. Request(1) returns zero until the message thread has
 * finished its last GDI blit and acknowledged. Calls are nonblocking. Release
 * the presenter before Request(0), which permits GDI again. The published HWND
 * is cleared when closing begins; the message thread never waits for D3D. */
void *xbox_FramebufferWindowHandle(void);
int xbox_FramebufferWindowGpuRequest(int enable);
int xbox_FramebufferWindowGpuActive(void);

#endif
