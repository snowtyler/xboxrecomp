/* The Xbox D3D screen-space epilogue, as every title's programs end:
 *
 *   mov o[0], v0                      position (stand-in for the DP4s)
 *   mul o[0].xyz, R12, c[58]  + rcc R1.x, R12.w
 *   mad o[0].xyz, R12, R1.x, c[59]
 *
 * The rcc is paired with a MAC op that writes no temp, and must still land
 * in R1; sent to the instruction's temp field instead, R1.x stayed 0 and
 * every vertex came out at c[59]. */
#include "nv2a_vsh_interp.h"
#include "nv2a_vertex_cache.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

enum { T = 1, V = 2, C = 3 };             /* source mux */
#define XYZW 0x1B                         /* swizzle x,y,z,w */
#define WWWW 0xFF
#define XXXX 0x00

static void src(uint32_t w[4], int which, int mux, int reg, int swz)
{
    if (which == 0) { w[1] |= swz; w[2] |= (mux << 26) | (reg << 28); }
    else if (which == 1) { w[2] |= (swz << 17) | (mux << 11) | (reg << 13); }
    else { w[2] |= (swz << 2) | (reg >> 2); w[3] |= (mux << 28) | ((reg & 3) << 30); }
}

int main(void)
{
    uint32_t i0[4] = {0}, i1[4] = {0}, i2[4] = {0};
    const float c58[4] = {320, -240, 1000, 0}, c59[4] = {320, 240, 0, 0};
    float in[NV2A_VSH_INPUTS][4] = {{0}};
    Nv2aVshOutput o;

    /* mov o[0].xyzw, v0 */
    i0[1] = (1u << 21);                         /* MAC mov, input v0 */
    src(i0, 0, V, 0, XYZW);
    i0[3] |= (0xFu << 12) | (1u << 11);         /* o[0] mask xyzw */

    /* mul o[0].xyz, R12, c[58] ; rcc R1.x, R12.w (temp field says R7) */
    i1[1] = (3u << 25) | (2u << 21) | (58u << 13);
    src(i1, 0, T, 12, XYZW); src(i1, 1, C, 0, XYZW); src(i1, 2, T, 12, WWWW);
    i1[3] |= (7u << 20) | (8u << 16) | (0xEu << 12) | (1u << 11);

    /* mad o[0].xyz, R12, R1.x, c[59] ; final */
    i2[1] = (4u << 21) | (59u << 13);
    src(i2, 0, T, 12, XYZW); src(i2, 1, T, 1, XXXX); src(i2, 2, C, 0, XYZW);
    i2[3] |= (0xEu << 12) | (1u << 11) | 1u;

    nv2a_vsh_set_instruction(0, i0);
    nv2a_vsh_set_instruction(1, i1);
    nv2a_vsh_set_instruction(2, i2);
    nv2a_vsh_set_constant(58, c58);
    nv2a_vsh_set_constant(59, c59);

    in[0][0] = 1.0f; in[0][1] = 0.5f; in[0][2] = 0.25f; in[0][3] = 2.0f;
    if (!nv2a_vsh_run((const float (*)[4])in, &o)) {
        puts("FAIL: program did not run");
        return 1;
    }
    /* x = 1*320/2 + 320, y = 0.5*-240/2 + 240, z = 0.25*1000/2, w kept */
    if (fabsf(o.pos[0] - 480) > 1e-3f || fabsf(o.pos[1] - 180) > 1e-3f
        || fabsf(o.pos[2] - 125) > 1e-3f || o.pos[3] != 2.0f) {
        printf("FAIL: pos %g %g %g %g, want 480 180 125 2\n",
               o.pos[0], o.pos[1], o.pos[2], o.pos[3]);
        return 1;
    }
    /* A screen-space shader that leaves W unwritten must remain visible.
     * Run after a shader that wrote W=2, to also check per-vertex reset. */
    i0[3] = (0xEu << 12) | (1u << 11) | 1u;
    nv2a_vsh_set_instruction(0, i0);
    if (!nv2a_vsh_run((const float (*)[4])in, &o)
        || o.pos[0] != 1.0f || o.pos[1] != 0.5f
        || o.pos[2] != 0.25f || o.pos[3] != 1.0f) {
        puts("FAIL: xyz-only position shader lost default W=1");
        return 1;
    }
    puts("ok");
    /* Outputs writing shared shader constants cannot be reused. */
    nv2a_vsh_set_cxt_write(1);
    assert(nv2a_vsh_cacheable());
    i0[3] = (0xFu << 12) | (22u << 3) | 1u;
    nv2a_vsh_set_instruction(0, i0);
    assert(!nv2a_vsh_cacheable());
    assert(nv2a_vsh_run((const float (*)[4])in, &o));
    assert(nv2a_vsh_constant(22)[0] == in[0][0]);
    nv2a_vsh_set_cxt_write(0);
    assert(nv2a_vsh_cacheable());
    /* Index collisions, full-width keys, per-batch invalidation, rollover,
     * and a full table must preserve the first output association. */
    static Nv2aVertexCache cache;
    uint32_t previous, key;
    nv2a_vertex_cache_begin(&cache);
    assert(!nv2a_vertex_cache_lookup(&cache, 1, 0, &previous));
    assert(!nv2a_vertex_cache_lookup(&cache, 0x10001, 1, &previous));
    assert(nv2a_vertex_cache_lookup(&cache, 1, 2, &previous) && previous == 0);
    assert(nv2a_vertex_cache_lookup(&cache, 0x10001, 3, &previous) && previous == 1);
    assert(!nv2a_vertex_cache_lookup(&cache, UINT32_MAX, 4, &previous));
    assert(nv2a_vertex_cache_lookup(&cache, UINT32_MAX, 5, &previous) && previous == 4);
    nv2a_vertex_cache_begin(&cache);
    for (key = 0; key < NV2A_VERTEX_CACHE_SIZE; ++key)
        assert(!nv2a_vertex_cache_lookup(&cache, key, key, &previous));
    assert(!nv2a_vertex_cache_lookup(&cache, NV2A_VERTEX_CACHE_SIZE, 0, &previous));
    assert(nv2a_vertex_cache_lookup(&cache, 32768, 0, &previous) && previous == 32768);
    cache.epoch = UINT32_MAX;
    nv2a_vertex_cache_begin(&cache);
    assert(!nv2a_vertex_cache_lookup(&cache, 1, 42, &previous));
    assert(nv2a_vertex_cache_lookup(&cache, 1, 43, &previous) && previous == 42);
    puts("Vertex reuse and context-write guard passed");
    return 0;
}
