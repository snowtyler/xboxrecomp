#include "nv2a_hw_raster.h"
#include "nv2a_render_diagnostics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t pixels[32*32], image[4]={0xFFFF0000,0xFF00FF00,0xFF0000FF,0xFFFFFFFF};
static uint32_t sample(void *p,uint32_t face,uint32_t x,uint32_t y)
{(void)p;assert(!face&&x<2&&y<2);return image[y*2+x];}
static uint32_t center(void){return pixels[16*32+16];}
int main(void)
{
    _putenv_s("RECOMP_HW_RASTER","1");
    int clamp=nv2a_color_clamp_enabled(),semantics=nv2a_v015_render_enabled();
    Nv2aHwState s={0};Nv2aVshOutput v[4]={0};uint32_t ix[]={0,1,2,2,1,3};
    s.surface=1;s.width=s.height=s.clip_w=s.clip_h=32;s.pitch=128;s.bpp=4;
    s.color=(unsigned char*)pixels;s.depth_scale=10;s.color_mask=0x01010101;
    s.use_rc=1;s.rc.final0=4;s.rc.final1=0x14<<8;
    for(unsigned i=0;i<4;i++) {
        v[i].pos[0]=(i&1)?32:0;v[i].pos[1]=(i&2)?32:0;v[i].pos[2]=2;v[i].pos[3]=1;
        v[i].d0[0]=(i&1)?2:0;v[i].d0[3]=1;
    }
    assert(nv2a_hw_draw(&s,v,4,ix,6)&&nv2a_hw_sync());
    unsigned red=(center()>>16)&255;
    assert(clamp?(red>125&&red<140):red==255);
    /* Same interpolation check through the raw hardware vertex program. */
    Nv2aVshSnapshot program={0};program.count=2;
    program.words[0][1]=(1u<<21)|0x1B;program.words[0][2]=2u<<26;
    program.words[0][3]=(15u<<12)|(1u<<11);
    program.words[1][1]=(1u<<21)|(3u<<9)|0x1B;program.words[1][2]=2u<<26;
    program.words[1][3]=(15u<<12)|(1u<<11)|(3u<<3)|1u;
    float raw[4][16][4]={0};
    for(unsigned i=0;i<4;i++)for(unsigned k=0;k<4;k++){raw[i][0][k]=v[i].pos[k];raw[i][3][k]=v[i].d0[k];}
    assert(nv2a_hw_draw_raw(&s,raw,4,ix,6,&program)&&nv2a_hw_sync());
    red=(center()>>16)&255;assert(clamp?(red>125&&red<140):red==255);
    /* CPU interpreter's oD0/oD1 boundary, including negative and >1 lanes. */
    uint32_t mov[4]={0,(1u<<21)|0x1B,2u<<26,(15u<<12)|(1u<<11)|(3u<<3)|1u};
    nv2a_vsh_set_instruction(0,mov);
    float in[16][4]={{-2,.25f,2,1}};Nv2aVshOutput out;
    assert(nv2a_vsh_run(in,&out));
    assert(out.d0[0]==(clamp?0:-2)&&out.d0[1]==.25f&&out.d0[2]==(clamp?1:2));
    mov[3]=(15u<<12)|(1u<<11)|(4u<<3)|1u;nv2a_vsh_set_instruction(0,mov);
    assert(nv2a_vsh_run(in,&out));assert(out.d1[0]==(clamp?0:-2)&&out.d1[2]==(clamp?1:2));
    s.use_rc=0;
    s.textures[0].width=s.textures[0].height=2;s.textures[0].raw=image;
    s.textures[0].raw_bytes=sizeof image;s.textures[0].sample=sample;
    for(unsigned i=0;i<4;i++) {
        for(unsigned k=0;k<4;k++)v[i].d0[k]=1;
        v[i].tex[0][0]=1.75f;v[i].tex[0][1]=.25f;v[i].tex[0][3]=1;
    }
    s.textures[0].addr_u=2;
    assert(nv2a_hw_draw(&s,v,4,ix,6)&&nv2a_hw_sync());
    assert(center()==(semantics?0xFFFF0000:0xFF00FF00));
    s.textures[0].addr_u=4;s.textures[0].border_color=1;s.textures[0].border=0xFF2244CC;
    assert(nv2a_hw_draw(&s,v,4,ix,6)&&nv2a_hw_sync());
    assert(center()==(semantics?0xFF2244CC:0xFF00FF00));
    s.textures[0].border=0xFFCC4422;
    assert(nv2a_hw_draw(&s,v,4,ix,6)&&nv2a_hw_sync());
    assert(center()==(semantics?0xFFCC4422:0xFF00FF00));
    nv2a_hw_shutdown();puts("Pre-interpolation CPU/raw-GPU colors, mirror and border diagnostics passed");return 0;
}
