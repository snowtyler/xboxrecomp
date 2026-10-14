#ifndef NV2A_HW_RASTER_H
#define NV2A_HW_RASTER_H
#include <stddef.h>
#include "nv2a_vsh_interp.h"
#include "nv2a_combiner.h"
typedef struct {
    uint32_t width, height, cube, linear, filter, addr_u, addr_v, format;
    const void *raw;
    size_t raw_bytes;
    const void *palette;
    size_t palette_bytes;
    void *opaque;
    uint32_t (*sample)(void *opaque, uint32_t face, uint32_t x, uint32_t y);
    uint32_t levels;
    /* Opt in only when linear LIN_A8R8G8B8 bytes match BGRA8 directly.
     * Nonzero permits a bounds-checked single-level upload using this pitch. */
    uint32_t bgra_pitch;
    uint32_t (*sample_mip)(void *opaque, uint32_t face, uint32_t level,
                           uint32_t x, uint32_t y);
} Nv2aHwTexture;
typedef struct {
    uint32_t surface, zeta, width, height, pitch, bpp;
    uint32_t clip_x, clip_y, clip_w, clip_h;
    unsigned char *color;
    float *depth; /* native depth, with stride 1024 */
    /* Nonzero: caller tracks CPU changes after synchronizing GPU writes.
     * Zero: unmanaged backing must be refreshed after every synchronization. */
    uint64_t depth_revision;
    float depth_scale, fog[4], fog_params[4];
    uint32_t depth_test, depth_mask, depth_func;
    uint32_t alpha_test, alpha_func, alpha_ref, clip_plane_mode;
    uint32_t blend, blend_src, blend_dst, blend_eq, blend_color, color_mask;
    int use_rc;
    Nv2aCombiner rc;
    Nv2aHwTexture textures[4];
} Nv2aHwState;
/* These are the texture modes implemented by the matching CPU/HLSL stage
 * evaluators. NONE, passthrough and clip-plane stages do not read texels. */
static inline int nv2a_hw_texture_used(const Nv2aHwState *state, unsigned stage)
{
    if (stage >= 4) return 0;
    if (!state->use_rc) return stage == 0;
    unsigned mode = (state->rc.stage_program >> (stage * 5)) & 31u;
    return mode >= 1 && mode <= 3;
}
int nv2a_hw_enabled(void);
/* Triangle-list indices refer to the supplied transformed vertices. Fog.x is
 * the fixed-function factor. Return zero before submitting if unsupported. */
int nv2a_hw_draw(const Nv2aHwState *state, const Nv2aVshOutput *vertices,
                 uint32_t count, const uint32_t *indices, uint32_t index_count);
int nv2a_hw_draw_raw(const Nv2aHwState *state,const float (*inputs)[16][4],
                     uint32_t count,const uint32_t *indices,uint32_t index_count,
                     const Nv2aVshSnapshot *program);
/* Synchronize GPU-written color/depth before CPU access or presentation. */
int nv2a_hw_sync(void);
/* Complete GPU work and publish color. Managed depth stays on the GPU until
 * a CPU raster/clear or target transition requests full synchronization. */
int nv2a_hw_complete(void);
/* Borrowed D3D11 objects for presentation on the renderer's calling thread.
 * A matching, completed 32-bit target is returned only while its CPU backing
 * still matches the synchronized snapshot. Otherwise resource is NULL and
 * the caller can upload the provided CPU pixels using the returned device.
 * A zero result leaves all outputs NULL (no hardware presentation device). */
int nv2a_hw_present_source(const unsigned char *color,uint32_t pitch,uint32_t width,uint32_t height,
                           void **device,void **context,void **resource);
void nv2a_hw_shutdown(void);
#endif
