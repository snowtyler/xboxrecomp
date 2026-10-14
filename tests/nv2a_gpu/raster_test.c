#include "nv2a_hw_raster.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static uint32_t pixels[32*32];
static float depth[1024*32];
static uint32_t image[4]={0xFFFF0000,0xFF00FF00,0xFF0000FF,0xFFFFFFFF};
static uint32_t sample(void *opaque,uint32_t face,uint32_t x,uint32_t y)
{ (void)opaque;(void)face;return image[y*2+x]; }
static unsigned unused_samples;
static uint32_t mip_colors[3]={0xFFFF0000,0xFF00FF00,0xFF0000FF};
static uint32_t sample_mip(void *opaque,uint32_t face,uint32_t level,uint32_t x,uint32_t y)
{(void)opaque;assert(face==0&&level<3&&x<(4u>>level)&&y<(4u>>level));return mip_colors[level];}
static uint32_t unused_sample(void *opaque,uint32_t face,uint32_t x,uint32_t y)
{(void)opaque;(void)face;(void)x;(void)y;++unused_samples;return 0;}
static uint32_t bgra_rows[6]={0x50102030,0xFFA0B0C0,0xDEADBEEF,0xFF405060,0x80224466,0xBADC0FFE};
static unsigned bgra_samples;
static uint32_t bgra_sample(void *opaque,uint32_t face,uint32_t x,uint32_t y)
{(void)opaque;assert(face==0&&x<2&&y<2);++bgra_samples;return bgra_rows[y*3+x];}
static uint32_t bgra_alternate_sample(void *opaque,uint32_t face,uint32_t x,uint32_t y)
{return bgra_sample(opaque,face,x,y)^0x00FFFFFF;}
int main(void)
{
    _putenv_s("RECOMP_HW_RASTER","1");
    Nv2aHwState s={0};Nv2aVshOutput v[4]={0};uint32_t indices[]={0,1,2,2,1,3};unsigned i;
    s.surface=1;s.zeta=2;s.width=s.height=s.clip_w=s.clip_h=32;s.pitch=128;s.bpp=4;
    s.color=(unsigned char*)pixels;s.depth=depth;s.depth_scale=10;s.depth_test=s.depth_mask=1;s.depth_func=0x201;s.color_mask=0x01010101;
    s.use_rc=1;s.rc.final0=4;s.rc.final1=0x14<<8;
    for(i=0;i<1024;i++)pixels[i]=0xFF0000FF;
    for(i=0;i<1024*32;i++)depth[i]=10;
    for(i=0;i<4;i++){v[i].pos[0]=(i&1)?32:0;v[i].pos[1]=(i&2)?32:0;v[i].pos[2]=2;v[i].pos[3]=1;v[i].d0[0]=v[i].d0[3]=1;v[i].fog[0]=1;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(pixels[16*32+16]==0xFFFF0000);assert(fabsf(depth[16*1024+16]-2)<0.001f);
    /* A farther draw fails depth and preserves both color and native depth. */
    for(i=0;i<4;i++){v[i].pos[2]=5;v[i].d0[0]=0;v[i].d0[2]=1;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(pixels[16*32+16]==0xFFFF0000);
    /* CPU-written movie pixels must be uploaded before an overlay. */
    for(i=0;i<1024;i++)pixels[i]=0xFF00FF00;
    s.depth_test=s.depth_mask=0;s.blend=1;s.blend_src=0x302;s.blend_dst=0x303;
    s.clip_w=s.clip_h=16;
    for(i=0;i<4;i++)v[i].d0[3]=0.5f;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    uint32_t p=pixels[8*32+8];assert(abs((int)(p&255)-128)<=1);assert(abs((int)((p>>8)&255)-128)<=1);
    assert(pixels[24*32+24]==0xFF00FF00);
    /* Projective fetch, texture cache freshness, and decoded BGRA channels. */
    s.blend=0;s.clip_w=s.clip_h=32;s.rc.stage_program=1;s.rc.final0=8;s.rc.final1=0x18<<8;
    s.textures[3].width=s.textures[3].height=1;
    s.textures[3].raw=image;s.textures[3].raw_bytes=4;s.textures[3].sample=unused_sample;
    s.textures[0].width=s.textures[0].height=2;s.textures[0].raw=image;s.textures[0].raw_bytes=sizeof image;s.textures[0].sample=sample;
    for(i=0;i<4;i++){v[i].tex[0][0]=(i&1)?1:0;v[i].tex[0][1]=(i&2)?1:0;v[i].tex[0][3]=1;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(pixels[8*32+8]==image[0]);assert(pixels[8*32+24]==image[1]);assert(pixels[24*32+8]==image[2]);assert(pixels[24*32+24]==image[3]);
    image[0]=0xFF102030;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(pixels[8*32+8]==image[0]);
    /* Surface switching downloads prior GPU work before selecting new backing. */
    static uint32_t other[32*32];s.surface=3;s.color=(unsigned char*)other;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(other[8*32+8]==image[0]);
    /* Direct vertex-program execution, with canonical raw attributes. */
    Nv2aVshSnapshot program={0};program.count=1;
    program.words[0][1]=(1u<<21)|0x1B;program.words[0][2]=2u<<26;
    program.words[0][3]=(15u<<12)|(1u<<11)|1u;
    float raw[4][16][4]={0};
    for(i=0;i<4;i++){raw[i][0][0]=v[i].pos[0];raw[i][0][1]=v[i].pos[1];raw[i][0][2]=2;raw[i][0][3]=1;}
    s.rc.stage_program=0;s.rc.final0=1;s.rc.final1=0x1100;s.rc.final_c0=0xFF4080C0;
    assert(nv2a_hw_draw_raw(&s,raw,4,indices,6,&program));assert(nv2a_hw_sync());
    assert(other[16*32+16]==0xFF4080C0);
    /* A stateful variant of an already-cached program cannot use that cache. */
    program.context_write=1;program.words[0][3]=(15u<<12)|(16u<<3)|1u;
    assert(!nv2a_hw_draw_raw(&s,raw,4,indices,6,&program));
    /* A depth-only pass must not copy its untouched color snapshot back.
     * Brute Force uses a dummy color address here, shared with CPU command
     * storage. A CPU mutation while that pass runs must survive readback. */
    s.color_mask=0;s.depth_test=s.depth_mask=1;s.depth_func=0x207;
    for(i=0;i<4;i++)v[i].pos[2]=1;
    assert(nv2a_hw_draw(&s,v,4,indices,6));
    other[16*32+16]=0xAABBCCDD;
    assert(nv2a_hw_sync());
    assert(other[16*32+16]==0xAABBCCDD);
    assert(fabsf(depth[16*1024+16]-1)<0.001f);
    /* Conversely a color-only pass must preserve CPU changes to unused depth. */
    s.color_mask=0x01010101;s.depth_test=s.depth_mask=0;
    assert(nv2a_hw_draw(&s,v,4,indices,6));
    depth[16*1024+16]=7;
    assert(nv2a_hw_sync());assert(depth[16*1024+16]==7);
    /* Disabling depth testing ignores the depth-write mask. */
    s.depth_test=0;s.depth_mask=1;depth[16*1024+16]=9;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(depth[16*1024+16]==9);
    /* Pending depth survives a later color-only draw with no depth pointer. */
    s.depth_test=s.depth_mask=1;s.depth_func=0x207;s.color_mask=0;
    assert(nv2a_hw_draw(&s,v,4,indices,6));
    s.depth=NULL;s.depth_test=s.depth_mask=0;s.color_mask=0x01010101;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(fabsf(depth[16*1024+16]-1)<0.001f);
    s.depth=depth;
    /* Hundreds of small draws wrap both vertex and index append regions.
     * Each draw also changes shader constants; the final pixels must use the
     * last upload rather than an earlier buffer segment or cached uniform. */
    for(unsigned draw=0;draw<220;draw++) {
        s.rc.final_c0=0xFF000000u|draw;
        assert(nv2a_hw_draw(&s,v,4,indices,6));
    }
    assert(nv2a_hw_sync());assert(other[16*32+16]==(0xFF000000u|219));
    assert(unused_samples==0); /* Stage 3 is NONE: no decoding or texel reads. */
    /* Managed depth stays coherent across syncs, then honors an explicit CPU
     * clear revision. A farther draw must fail before that clear and pass after. */
    s.depth_revision=1;s.depth_test=s.depth_mask=1;s.depth_func=0x201;
    s.rc.final_c0=0xFF102030;
    for(i=0;i<4;i++)v[i].pos[2]=0.5f;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    for(i=0;i<4;i++)v[i].pos[2]=2;
    s.rc.final_c0=0xFF405060;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==0xFF102030);
    for(i=0;i<sizeof depth/sizeof depth[0];i++)depth[i]=8;
    ++s.depth_revision;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==0xFF405060);
    assert(fabsf(depth[16*1024+16]-2)<0.001f);
    /* Guest fences complete managed depth-only GPU work without reading the
     * private CPU depth plane. A later CPU-access sync must still download it. */
    s.color_mask=0;s.depth_func=0x207;
    for(i=0;i<4;i++)v[i].pos[2]=0.25f;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_complete());
    assert(depth[16*1024+16]==2);
    assert(nv2a_hw_complete());assert(nv2a_hw_sync());
    assert(fabsf(depth[16*1024+16]-0.25f)<0.001f);
    s.color_mask=0x01010101;s.rc.final_c0=0xFF789ABC;
    for(i=0;i<4;i++)v[i].pos[2]=0.125f;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_complete());
    assert(other[16*32+16]==0xFF789ABC);
    assert(fabsf(depth[16*1024+16]-0.25f)<0.001f);
    assert(nv2a_hw_sync());assert(fabsf(depth[16*1024+16]-0.125f)<0.001f);
    /* A retained color target must still honor CPU writes between fences,
     * including channels preserved by the following partial-mask draw. */
    s.depth_test=s.depth_mask=0;s.color_mask=0x10000;
    s.rc.final_c0=0xFFABCDEF;other[16*32+16]=0xFF112233;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_complete());
    assert(other[16*32+16]==0xFFAB2233);
    assert(nv2a_hw_sync()); /* No dirty draw; CPU access can follow this sync. */
    other[16*32+16]=0xFF445566;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==0xFFAB5566);
    s.color_mask=0x01010101;
    /* Distinct colors make derivative-based mip selection observable. The
     * 4x4 texture repeats 16 times over 32 pixels: LOD is exactly one. */
    s.depth_test=s.depth_mask=0;s.rc.stage_program=1;s.rc.final0=8;s.rc.final1=0x1800;
    s.textures[0].width=s.textures[0].height=4;s.textures[0].levels=3;
    s.textures[0].sample_mip=sample_mip;s.textures[0].raw=mip_colors;
    s.textures[0].raw_bytes=sizeof mip_colors;s.textures[0].filter=(3u<<16)|(1u<<24);
    s.textures[0].addr_u=s.textures[0].addr_v=1;
    for(i=0;i<4;i++){v[i].tex[0][0]=(i&1)?16:0;v[i].tex[0][1]=(i&2)?16:0;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==mip_colors[1]);
    mip_colors[1]=0xFFAABBCC; /* A mutation in a lower mip invalidates cache. */
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==mip_colors[1]);
    s.textures[0].filter=(1u<<16)|(1u<<24); /* Nonmip filter pins level zero. */
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[16*32+16]==mip_colors[0]);
    /* Opted-in linear BGRA upload consumes row pitch directly, including
     * alpha, without sampling padding or calling the generic decoder. */
    s.textures[0]=(Nv2aHwTexture){0};
    s.textures[0].width=s.textures[0].height=2;s.textures[0].linear=1;
    s.textures[0].format=0x12;s.textures[0].levels=1;s.textures[0].bgra_pitch=12;
    s.textures[0].raw=bgra_rows;s.textures[0].raw_bytes=sizeof bgra_rows;s.textures[0].sample=bgra_sample;
    s.textures[0].filter=(1u<<16)|(1u<<24);
    for(i=0;i<4;i++){v[i].tex[0][0]=(i&1)?2:0;v[i].tex[0][1]=(i&2)?2:0;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(bgra_samples==0);
    assert(other[8*32+8]==bgra_rows[0]);assert(other[8*32+24]==bgra_rows[1]);
    assert(other[24*32+8]==bgra_rows[3]);assert(other[24*32+24]==bgra_rows[4]);
    bgra_rows[4]=0x60ABCDEF;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[24*32+24]==bgra_rows[4]);assert(bgra_samples==0);
    bgra_rows[2]^=0x00FFFFFF; /* Padding remains outside visible texels. */
    s.textures[0].raw_bytes=20; /* Last-row padding is not required. */
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());
    assert(other[8*32+8]==bgra_rows[0]);assert(other[24*32+24]==bgra_rows[4]);assert(bgra_samples==0);
    s.textures[0].raw_bytes=19; /* An incomplete last pixel uses the callback. */
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(bgra_samples==4);
    assert(other[24*32+24]==bgra_rows[4]);
    s.textures[0].raw_bytes=sizeof bgra_rows;s.textures[0].bgra_pitch=4;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(bgra_samples==8);
    s.textures[0].bgra_pitch=0; /* Generic descriptors retain their callback semantics. */
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(bgra_samples==12);
    /* Linear eligibility is part of the cache identity. An unchanged raw
     * snapshot cannot reuse a direct upload when generic sampling is needed. */
    s.textures[0].bgra_pitch=12;s.textures[0].sample=bgra_alternate_sample;
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(bgra_samples==12);
    s.textures[0].linear=0;
    for(i=0;i<4;i++){v[i].tex[0][0]=(i&1)?1:0;v[i].tex[0][1]=(i&2)?1:0;}
    assert(nv2a_hw_draw(&s,v,4,indices,6));assert(nv2a_hw_sync());assert(bgra_samples==16);
    assert(other[8*32+8]==(bgra_rows[0]^0x00FFFFFF));
    assert(other[24*32+24]==(bgra_rows[4]^0x00FFFFFF));

    /* GPU presentation borrows only a matching, completed, unchanged BGRA
     * target. CPU movie pixels and mismatched layouts request an upload. */
    void *device,*context,*resource;
    int retained=getenv("RECOMP_HW_COLOR_RELOAD")==NULL;
    assert(nv2a_hw_present_source(s.color,s.pitch,s.width,s.height,&device,&context,&resource));
    assert(device&&context&&(!!resource==retained));
    uint32_t saved=other[16*32+16];other[16*32+16]^=1;
    assert(nv2a_hw_present_source(s.color,s.pitch,s.width,s.height,&device,&context,&resource));assert(!resource);
    other[16*32+16]=saved;
    assert(nv2a_hw_present_source(s.color,s.pitch+4,s.width,s.height,&device,&context,&resource));assert(!resource);
    assert(nv2a_hw_present_source((const unsigned char*)pixels,s.pitch,s.width,s.height,&device,&context,&resource));assert(!resource);

    /* A target switch publishes pending work into the old backing before
     * reusing its same-sized native allocation. The new partial mask must
     * preserve the new backing's untouched channels. */
    Nv2aHwState r={0};r.width=r.height=r.clip_w=r.clip_h=32;r.pitch=128;r.bpp=4;
    r.depth_scale=10;r.color_mask=0x01010101;r.use_rc=1;r.rc.final0=1;r.rc.final1=0x1100;
    r.surface=10;r.color=(unsigned char*)pixels;r.rc.final_c0=0xFF102030;
    assert(nv2a_hw_draw(&r,v,4,indices,6));
    assert(nv2a_hw_present_source(r.color,r.pitch,r.width,r.height,&device,&context,&resource));assert(!resource);
    for(i=0;i<1024;i++)other[i]=0xFF445566;
    r.surface=11;r.color=(unsigned char*)other;r.color_mask=0x10000;r.rc.final_c0=0xFFAABBCC;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(pixels[16*32+16]==0xFF102030);
    assert(nv2a_hw_complete());assert(other[16*32+16]==0xFFAA5566);

    /* A depth-only target switch cannot validate an old surface's retained
     * CPU snapshot, even when its allocation and snapshot storage survive. */
    static uint32_t third[32*32];static float third_depth[1024*32],fourth_depth[1024*32];
    for(i=0;i<1024;i++)third[i]=0xFFE1D2C3;
    for(i=0;i<1024*32;i++){third_depth[i]=10;fourth_depth[i]=8;}
    r.surface=12;r.color=(unsigned char*)third;r.zeta=13;r.depth=third_depth;r.depth_revision=1;
    r.color_mask=0;r.depth_test=r.depth_mask=1;r.depth_func=0x207;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_complete());assert(third[16*32+16]==0xFFE1D2C3);
    r.depth_test=r.depth_mask=0;r.color_mask=0x10000;r.rc.final_c0=0xFF781122;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_sync());assert(third[16*32+16]==0xFF78D2C3);

    /* Reusing a same-sized depth allocation downloads its previous backing
     * and uploads the new plane before evaluating a depth-tested draw. */
    r.depth_test=r.depth_mask=1;r.color_mask=0;
    for(i=0;i<4;i++)v[i].pos[2]=0.25f;
    assert(nv2a_hw_draw(&r,v,4,indices,6));
    r.zeta=14;r.depth=fourth_depth;r.depth_revision=2;r.depth_func=0x201;
    for(i=0;i<4;i++)v[i].pos[2]=2;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(fabsf(third_depth[16*1024+16]-0.25f)<0.001f);
    assert(nv2a_hw_complete());assert(fourth_depth[16*1024+16]==8);
    assert(nv2a_hw_sync());assert(fabsf(fourth_depth[16*1024+16]-2)<0.001f);

    /* Changing pitch alone publishes pending rows using the old layout,
     * then preserves the new layout's channels on a partial-mask draw. */
    static uint32_t padded[40*32];for(i=0;i<40*32;i++)padded[i]=0xFF112233;
    r.surface=15;r.color=(unsigned char*)padded;r.depth=NULL;r.depth_test=r.depth_mask=0;
    r.color_mask=0x01010101;r.rc.final_c0=0xFFEEDDCC;r.pitch=128;
    assert(nv2a_hw_draw(&r,v,4,indices,6));
    r.pitch=160;r.color_mask=0x10000;r.rc.final_c0=0xFFAA0000;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(padded[31*32+16]==0xFFEEDDCC);
    assert(nv2a_hw_sync());assert(padded[31*40+16]==0xFFAA2233);assert(padded[8*40+8]==0xFFAADDCC);
    assert(nv2a_hw_present_source(r.color,r.pitch,r.width,r.height,&device,&context,&resource));assert(!!resource==retained);

    /* Sixteen-bit reuse retains the existing quantized readback semantics. */
    static uint16_t rgb565_a[32*32],rgb565_b[32*32];
    for(i=0;i<1024;i++)rgb565_b[i]=0x07E0;
    r.surface=16;r.color=(unsigned char*)rgb565_a;r.pitch=64;r.bpp=2;r.color_mask=0x01010101;r.rc.final_c0=0xFF0000FF;
    assert(nv2a_hw_draw(&r,v,4,indices,6));
    r.surface=17;r.color=(unsigned char*)rgb565_b;r.color_mask=0x10000;r.rc.final_c0=0xFFFF0000;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(rgb565_a[16*32+16]==0x001F);
    assert(nv2a_hw_sync());assert(rgb565_b[16*32+16]==0xFFE0);
    assert(nv2a_hw_present_source(r.color,r.pitch,r.width,r.height,&device,&context,&resource));assert(!resource);
    /* A new managed-depth revision must not invalidate color after its
     * upload. The first half-frame survives a second draw before any fence. */
    for(i=0;i<1024;i++)third[i]=0xFF112233;
    for(i=0;i<1024*32;i++)third_depth[i]=10;
    r.surface=18;r.color=(unsigned char*)third;r.pitch=128;r.bpp=4;
    r.zeta=19;r.depth=third_depth;r.depth_revision=99;r.depth_test=r.depth_mask=1;r.depth_func=0x207;
    r.color_mask=0x01010101;r.clip_x=0;r.clip_w=16;r.rc.final_c0=0xFFFF0000;
    assert(nv2a_hw_draw(&r,v,4,indices,6));
    r.clip_x=16;r.rc.final_c0=0xFF00FF00;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_sync());
    assert(third[16*32+8]==0xFFFF0000);assert(third[16*32+24]==0xFF00FF00);
    /* A setup failure after uploading CPU changes leaves no valid GPU/CPU
     * snapshot. Even if CPU fallback restores the previous readback bytes,
     * presentation and the next overlay cannot trust the changed GPU image. */
    for(i=0;i<1024;i++)third[i]=0xFF112233;
    Nv2aHwState rejected=r;rejected.rc.stage_program=1;
    rejected.textures[0].width=65536;rejected.textures[0].height=1; /* Beyond D3D11 texture limits. */
    assert(!nv2a_hw_draw(&rejected,v,4,indices,6));assert(nv2a_hw_sync());
    for(i=0;i<1024;i++)third[i]=(i%32<16)?0xFFFF0000:0xFF00FF00;
    assert(nv2a_hw_present_source(r.color,r.pitch,r.width,r.height,&device,&context,&resource));assert(!resource);
    r.depth_test=r.depth_mask=0;r.clip_x=0;r.clip_w=32;r.color_mask=0x10000;r.rc.final_c0=0xFFAA0000;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_sync());
    assert(third[16*32+8]==0xFFAA0000);assert(third[16*32+24]==0xFFAAFF00);
    /* Alternate dimensions beyond the bounded allocation pool's capacity,
     * then revisit them in reverse order. Every reuse (or eviction) must
     * reload CPU color channels and the current managed-depth revision. */
    r.color=(unsigned char*)third;r.depth=third_depth;r.pitch=128;r.bpp=4;
    r.depth_test=r.depth_mask=1;r.depth_func=0x201;r.color_mask=0x10000;
    r.rc.final_c0=0xFFAA0000;r.clip_x=r.clip_y=0;
    for(unsigned pass=0;pass<28;pass++) {
        unsigned dimension=pass<14?pass:27-pass;
        uint32_t cpu_color=0xFF002233|((pass+1)<<16);
        float cpu_depth=pass&1?1.0f:10.0f;
        for(i=0;i<1024;i++)third[i]=cpu_color;
        for(i=0;i<1024*32;i++)third_depth[i]=cpu_depth;
        r.surface=20+pass;r.zeta=100+pass;r.depth_revision=100+pass;
        r.width=r.clip_w=16+dimension;r.height=r.clip_h=16+dimension%3;
        for(i=0;i<4;i++)v[i].pos[2]=2;
        assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_complete());
        assert(third[8*32+8]==(pass&1?cpu_color:0xFFAA2233));
        assert(third[31*32+31]==cpu_color); /* Outside the new target. */
        assert(nv2a_hw_sync());
        assert(fabsf(third_depth[8*1024+8]-(pass&1?1.0f:2.0f))<0.001f);
        assert(third_depth[31*1024+31]==cpu_depth);
    }
    /* A wide, tightly packed BGRA target exercises contiguous readback.
     * The last pixel and a CPU edit there must survive exact-snapshot checks. */
    static uint32_t wide[256*8];
    Nv2aHwState packed={0};packed.surface=200;packed.width=packed.clip_w=256;
    packed.height=packed.clip_h=8;packed.pitch=1024;packed.bpp=4;
    packed.color=(unsigned char*)wide;packed.depth_scale=10;packed.color_mask=0x10000;
    packed.use_rc=1;packed.rc.final0=1;packed.rc.final1=0x1100;packed.rc.final_c0=0xFF660000;
    for(i=0;i<256*8;i++)wide[i]=0xFE114477;
    for(i=0;i<4;i++){v[i].pos[0]=(i&1)?256:0;v[i].pos[1]=(i&2)?8:0;}
    assert(nv2a_hw_draw(&packed,v,4,indices,6));assert(nv2a_hw_sync());
    for(i=0;i<256*8;i++)assert(wide[i]==0xFE664477);
    wide[256*8-1]=0xFE22AA99;packed.rc.final_c0=0xFF550000;
    assert(nv2a_hw_draw(&packed,v,4,indices,6));assert(nv2a_hw_sync());
    assert(wide[256*8-1]==0xFE55AA99);assert(wide[0]==0xFE554477);
    for(i=0;i<4;i++){v[i].pos[0]=(i&1)?32:0;v[i].pos[1]=(i&2)?32:0;}
    nv2a_hw_shutdown();
    /* Shutdown releases every pooled resource along with the device. A fresh
     * device must initialize from the new backing, with no retained snapshot. */
    for(i=0;i<1024;i++)third[i]=0xFE998877;
    for(i=0;i<1024*32;i++)third_depth[i]=10;
    ++r.depth_revision;
    assert(nv2a_hw_draw(&r,v,4,indices,6));assert(nv2a_hw_sync());
    assert(third[8*32+8]==0xFEAA8877);
    assert(fabsf(third_depth[8*1024+8]-2)<0.001f);
    nv2a_hw_shutdown();puts("Hardware raster, coherence, bounded target pool, direct BGRA, mip freshness and presentation source passed");return 0;
}
