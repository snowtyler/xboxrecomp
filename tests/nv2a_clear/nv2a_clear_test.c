#include <assert.h>
#include <stdio.h>
#include "nv2a_clear_rect.h"

int main(void)
{
    uint32_t color[16 * 12];
    uint16_t small[16 * 12];
    float depth[16 * 12];
    Nv2aClearRect r;
    unsigned x, y;
    assert(nv2a_clear_bounds(15u << 16, 11u << 16, 0, 0, 16, 12, &r));
    nv2a_clear_color_rect(color, 16 * 4, 4, &r, 0xFF2674A1);
    nv2a_clear_depth_rect(depth, 16, 12, r, 16777215.0f);
    assert(nv2a_clear_bounds(15u << 16, 2u << 16, 0, 0, 16, 12, &r));
    nv2a_clear_color_rect(color, 16 * 4, 4, &r, 0);
    nv2a_clear_depth_rect(depth, 16, 12, r, 0);
    assert(nv2a_clear_bounds(15u << 16, (11u << 16) | 9u, 0, 0, 16, 12, &r));
    nv2a_clear_color_rect(color, 16 * 4, 4, &r, 0);
    nv2a_clear_depth_rect(depth, 16, 12, r, 0);
    for (y = 0; y < 12; ++y)
        for (x = 0; x < 16; ++x) {
            int middle = y >= 3 && y < 9;
            assert(color[y * 16 + x] == (middle ? 0xFF2674A1u : 0));
            assert(depth[y * 16 + x] == (middle ? 16777215.0f : 0));
        }
    assert(nv2a_clear_bounds(0xFFFF0000, 0xFFFF0000, 3, 3, 9, 6, &r));
    assert(r.left == 3 && r.top == 3 && r.right == 12 && r.bottom == 9);
    assert(!nv2a_clear_bounds((2u << 16) | 3u, 11u << 16, 0, 0, 16, 12, &r));
    assert(nv2a_clear_bounds(15u << 16, 11u << 16, 0, 0, 16, 12, &r));
    nv2a_clear_color_rect(small, 16 * 2, 2, &r, 0xFF12ABCD);
    for (x = 0; x < 16 * 12; ++x) assert(small[x] == 0x1559);
    puts("NV2A partial color/depth clears passed");
    return 0;
}
