#ifndef NV2A_CLEAR_RECT_H
#define NV2A_CLEAR_RECT_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t left, top, right, bottom; } Nv2aClearRect;

/* Clear maxima are inclusive; surface clip dimensions are extents. */
static inline int nv2a_clear_bounds(uint32_t horizontal, uint32_t vertical,
                                  uint32_t clip_x, uint32_t clip_y,
                                  uint32_t width, uint32_t height,
                                  Nv2aClearRect *r)
{
    r->left = horizontal & 0xFFFFu;
    r->right = (horizontal >> 16) + 1u;
    r->top = vertical & 0xFFFFu;
    r->bottom = (vertical >> 16) + 1u;
    if (r->left < clip_x) r->left = clip_x;
    if (r->top < clip_y) r->top = clip_y;
    if (r->right > clip_x + width) r->right = clip_x + width;
    if (r->bottom > clip_y + height) r->bottom = clip_y + height;
    return r->left < r->right && r->top < r->bottom;
}

static inline void nv2a_clear_color_rect(void *base, uint32_t pitch,
                                       uint32_t bpp, const Nv2aClearRect *r,
                                       uint32_t color)
{
    uint32_t x, y;
    for (y = r->top; y < r->bottom; ++y) {
        uint8_t *row = (uint8_t *)base + (size_t)y * pitch;
        if (bpp == 4) {
            uint32_t *p = (uint32_t *)row;
            for (x = r->left; x < r->right; ++x) p[x] = color;
        } else if (bpp == 2) {
            uint16_t v = (uint16_t)(((color >> 8) & 0xF800)
                                 | ((color >> 5) & 0x07E0)
                                 | ((color >> 3) & 0x001F));
            uint16_t *p = (uint16_t *)row;
            for (x = r->left; x < r->right; ++x) p[x] = v;
        }
    }
}

static inline void nv2a_clear_depth_rect(float *base, uint32_t width,
                                       uint32_t height, Nv2aClearRect r,
                                       float value)
{
    uint32_t x, y;
    if (r.right > width) r.right = width;
    if (r.bottom > height) r.bottom = height;
    for (y = r.top; y < r.bottom; ++y)
        for (x = r.left; x < r.right; ++x)
            base[(size_t)y * width + x] = value;
}
#endif
