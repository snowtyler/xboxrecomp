#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "nv2a_clip.h"

int main(void)
{
    float scale[3], offset[3];
    Nv2aVshOutput a = {0}, b = {0}, c = {0}, out[NV2A_CLIP_CAPACITY];
    unsigned n, i;
    nv2a_clip_window(0, 0, 640, 480, 0, 16777215, scale, offset);
    assert(scale[0] == 320 && scale[1] == -240 && scale[2] == 16777215);
    assert(offset[0] == 320 && offset[1] == 240 && offset[2] == 0);
    a.pos[0] = 100; a.pos[1] = 100; a.pos[2] = 12345678; a.pos[3] = 1;
    b = a; b.pos[0] = 200;
    c = a; c.pos[1] = 200;
    n = nv2a_clip_triangle(&a, &b, &c, scale, offset, out);
    assert(n == 3 && memcmp(&a, &out[0], sizeof a) == 0);
    assert(memcmp(&b, &out[1], sizeof b) == 0);
    assert(memcmp(&c, &out[2], sizeof c) == 0);
    a.pos[0] = -10000000; a.pos[1] = -10000000;
    a.pos[3] = -1; a.d0[0] = 1;
    n = nv2a_clip_triangle(&a, &b, &c, scale, offset, out);
    assert(n >= 3 && n <= 10);
    for (i = 0; i < n; ++i) {
        assert(isfinite(out[i].pos[0]) && isfinite(out[i].pos[1]));
        assert(out[i].pos[0] >= -0.1f && out[i].pos[0] <= 640.1f);
        assert(out[i].pos[1] >= -0.1f && out[i].pos[1] <= 480.1f);
        assert(out[i].pos[2] >= -1 && out[i].pos[2] <= 16777216);
        assert(out[i].pos[3] >= NV2A_CLIP_MIN_W * .999f);
        assert(out[i].d0[0] >= 0 && out[i].d0[0] <= 1);
    }
    a = b = c; a.pos[2] = b.pos[2] = c.pos[2] = -100;
    assert(nv2a_clip_triangle(&a, &b, &c, scale, offset, out) == 0);
    a.pos[2] = b.pos[2] = c.pos[2] = 16777300;
    assert(nv2a_clip_triangle(&a, &b, &c, scale, offset, out) == 0);
    a.pos[3] = b.pos[3] = c.pos[3] = -1;
    assert(nv2a_clip_triangle(&a, &b, &c, scale, offset, out) == 0);
    puts("NV2A homogeneous clipping passed");
    return 0;
}
