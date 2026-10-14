#ifndef NV2A_CLIP_H
#define NV2A_CLIP_H
#include "nv2a_vsh_interp.h"
#include <math.h>
#include <string.h>

#define NV2A_CLIP_CAPACITY 16
#define NV2A_CLIP_MIN_W 0.001f

static inline void nv2a_clip_window(uint32_t x, uint32_t y,
    uint32_t width, uint32_t height, float zmin, float zmax,
    float scale[3], float offset[3])
{
    scale[0] = (float)width * 0.5f;
    scale[1] = -(float)height * 0.5f;
    scale[2] = zmax - zmin;
    offset[0] = (float)x + scale[0];
    offset[1] = (float)y - scale[1];
    offset[2] = zmin;
}

static inline float nv2a_clip_distance(const Nv2aVshOutput *v, unsigned plane)
{
    const float *p = v->pos;
    switch (plane) {
    case 0: return p[3] - NV2A_CLIP_MIN_W;
    case 1: return p[0] + p[3];
    case 2: return p[3] - p[0];
    case 3: return p[1] + p[3];
    case 4: return p[3] - p[1];
    case 5: return p[2];
    default: return p[3] - p[2];
    }
}

/* Recover homogeneous coordinates from the Xbox shader's window-space
 * position, clip a convex polygon, then return window-space vertices.
 * Attributes remain linear in homogeneous space throughout clipping. */
static inline unsigned nv2a_clip_triangle(const Nv2aVshOutput *a,
    const Nv2aVshOutput *b, const Nv2aVshOutput *c,
    const float scale[3], const float offset[3],
    Nv2aVshOutput result[NV2A_CLIP_CAPACITY])
{
    Nv2aVshOutput buffers[2][NV2A_CLIP_CAPACITY];
    const Nv2aVshOutput *input[3] = {a, b, c};
    unsigned n = 3, current = 0, plane, i, j, outside = 0;
    for (j = 0; j < 3; ++j)
        if (!isfinite(scale[j]) || fabsf(scale[j]) < 1e-30f)
            return 0;
    for (i = 0; i < 3; ++i) {
        buffers[0][i] = *input[i];
        for (j = 0; j < 3; ++j)
            buffers[0][i].pos[j] = (input[i]->pos[j] - offset[j])
                                    / scale[j] * input[i]->pos[3];
        for (j = 0; j < 4; ++j)
            if (!isfinite(buffers[0][i].pos[j])) return 0;
        for (plane = 0; plane < 7; ++plane)
            if (nv2a_clip_distance(&buffers[0][i], plane) < 0) outside = 1;
    }
    /* Preserve exact depth values for unchanged triangles and later EQUAL
     * passes. A triangle gains at most one vertex per clipping plane. */
    if (!outside) {
        result[0] = *a; result[1] = *b; result[2] = *c;
        return 3;
    }
    for (plane = 0; plane < 7 && n; ++plane) {
        Nv2aVshOutput *src = buffers[current], *dst = buffers[current ^ 1];
        unsigned count = 0;
        for (i = 0; i < n; ++i) {
            const Nv2aVshOutput *p = &src[i], *q = &src[(i + 1) % n];
            float dp = nv2a_clip_distance(p, plane);
            float dq = nv2a_clip_distance(q, plane);
            int pin = dp >= 0, qin = dq >= 0;
            if (pin) dst[count++] = *p;
            if (pin != qin) {
                float t = dp / (dp - dq);
                const float *pf = (const float *)p, *qf = (const float *)q;
                float *out = (float *)&dst[count++];
                for (j = 0; j < sizeof(*p) / sizeof(float); ++j)
                    out[j] = pf[j] + (qf[j] - pf[j]) * t;
            }
        }
        n = count;
        current ^= 1;
    }
    for (i = 0; i < n; ++i) {
        result[i] = buffers[current][i];
        for (j = 0; j < 3; ++j)
            result[i].pos[j] = result[i].pos[j] / result[i].pos[3]
                                * scale[j] + offset[j];
    }
    return n;
}
#endif
