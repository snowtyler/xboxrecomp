#include "nv2a_combiner_hlsl.h"
#include <stdio.h>
#include <stdarg.h>
typedef struct { char *buf; size_t cap, length; int failed; } Writer;
static void emit(Writer *w, const char *format, ...)
{
    int n; va_list args;
    if (w->failed) return;
    va_start(args, format);
    n = vsnprintf(w->buf + w->length, w->cap - w->length, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->cap - w->length) { w->failed = 1; return; }
    w->length += n;
}
static void input(Writer *w, const char *name, uint32_t byte, int alpha)
{
    byte &= 255;
    emit(w, " %s = nv_map(r%u.%s, %u).%s;\n", name, byte & 15,
         byte & 16 ? "aaaa" : alpha ? "bbbb" : "xyzw", byte, alpha ? "x" : "rgb");
}
void nv2a_rc_gpu_constants(const Nv2aCombiner *rc, Nv2aRcGpuConstants *out)
{
    int s;
    for (s = 0; s < 8; ++s) {
        nv2a_rc_unpack(rc->factor0[s], out->factor0[s]);
        nv2a_rc_unpack(rc->factor1[s], out->factor1[s]);
    }
    nv2a_rc_unpack(rc->final_c0, out->final0);
    nv2a_rc_unpack(rc->final_c1, out->final1);
}
int nv2a_rc_hlsl(const Nv2aCombiner *rc, char *buffer, size_t capacity)
{
    Writer w = {buffer, capacity, 0, 0}; uint32_t n = rc->control & 255, flags = rc->control >> 8, s; int i;
    if (!buffer || !capacity) return -1;
    if (n > 8) n = 8;
    emit(&w, "cbuffer NV_RC_CONSTANTS:register(b0){float4 nv_c0[8];float4 nv_c1[8];float4 nv_final0;float4 nv_final1;};\n"
        "float4 nv_map(float4 x,uint b){float4 p=max(x,0);switch((b>>5)&7){"
        "case 0:return p;case 1:return 1-saturate(x);case 2:return 2*p-1;case 3:return 1-2*p;"
        "case 4:return p-0.5;case 5:return 0.5-p;case 6:return x;default:return -x;}}\n"
        "float4 nv_scale(float4 x,uint b){switch(b&56){case 8:return x-0.5;case 16:return x*2;"
        "case 24:return (x-0.5)*2;case 32:return x*4;case 48:return x*0.5;default:return x;}}\n"
        "float4 nv_combiner(float4 v0,float4 v1,float4 fog,float4 t0,float4 t1,float4 t2,float4 t3){\n");
    for (i = 0; i < 16; ++i) emit(&w, "float4 r%d=0;\n", i);
    emit(&w, "r3=fog;r4=v0;r5=v1;r8=t0;r9=t1;r10=t2;r11=t3;r12.a=%s;\n",
         rc->stage_program & 31 ? "t0.a" : "1");
    for (s = 0; s < n; ++s) {
        uint32_t oc = rc->color_ocw[s], ac = rc->alpha_ocw[s], fl = oc >> 12, af = ac >> 12;
        uint32_t ci = rc->color_icw[s], ai = rc->alpha_icw[s];
        emit(&w, "{r1=nv_c0[%u];r2=nv_c1[%u];bool mux_cd=%s;\n",
             flags & 16 ? s : 0, flags & 256 ? s : 0,
             flags & 1 ? "r12.a>=0.5" : "(((int)(r12.a*255.0))&1)!=0");
        input(&w, "float3 A", ci >> 24, 0); input(&w, "float3 B", ci >> 16, 0);
        input(&w, "float3 C", ci >> 8, 0); input(&w, "float3 D", ci, 0);
        input(&w, "float aA", ai >> 24, 1); input(&w, "float aB", ai >> 16, 1);
        input(&w, "float aC", ai >> 8, 1); input(&w, "float aD", ai, 1);
        emit(&w, "float3 pab=%s;float3 pcd=%s;float3 psum=%s;\n",
             fl & 2 ? "dot(A,B)" : "A*B", fl & 1 ? "dot(C,D)" : "C*D",
             fl & 4 ? "(mux_cd?pcd:pab)" : "pab+pcd");
        emit(&w, "float3 ab=clamp(nv_scale(float4(pab,0),%u).rgb,-1,1);"
             "float3 cd=clamp(nv_scale(float4(pcd,0),%u).rgb,-1,1);"
             "float3 ms=clamp(nv_scale(float4(psum,0),%u).rgb,-1,1);\n", fl, fl, fl);
        emit(&w, "float apab=aA*aB;float apcd=aC*aD;float asum=%s;"
             "float aab=clamp(nv_scale(apab,%u).x,-1,1);"
             "float acd=clamp(nv_scale(apcd,%u).x,-1,1);"
             "float ams=clamp(nv_scale(asum,%u).x,-1,1);\n",
             af & 4 ? "(mux_cd?apcd:apab)" : "apab+apcd", af, af, af);
        if ((oc >> 4) & 15) {
            emit(&w, "r%u.rgb=ab;", (oc >> 4) & 15);
            if (fl & 128) emit(&w, "r%u.a=ab.b;", (oc >> 4) & 15);
        }
        if (oc & 15) {
            emit(&w, "r%u.rgb=cd;", oc & 15);
            if (fl & 64) emit(&w, "r%u.a=cd.b;", oc & 15);
        }
        if ((oc >> 8) & 15) emit(&w, "r%u.rgb=ms;", (oc >> 8) & 15);
        if ((ac >> 4) & 15) emit(&w, "r%u.a=aab;", (ac >> 4) & 15);
        if (ac & 15) emit(&w, "r%u.a=acd;", ac & 15);
        if ((ac >> 8) & 15) emit(&w, "r%u.a=ams;", (ac >> 8) & 15);
        emit(&w, "r0=0;}\n");
    }
    if (rc->final0 || rc->final1) {
        uint32_t f = rc->final1 & 255;
        emit(&w, "r1=nv_final0;r2=nv_final1;r14.rgb=%s%s+%s%s;\n",
             f & 128 ? "saturate(" : "", f & 64 ? "(1-r5.rgb)" : "r5.rgb",
             f & 32 ? "(1-r12.rgb)" : "r12.rgb", f & 128 ? ")" : "");
        emit(&w, "r14.a=0;\n");
        input(&w, "float3 E", rc->final1 >> 24, 0); input(&w, "float3 F", rc->final1 >> 16, 0);
        emit(&w, "r15.rgb=E*F;r15.a=0;\n");
        input(&w, "float3 A", rc->final0 >> 24, 0); input(&w, "float3 B", rc->final0 >> 16, 0);
        input(&w, "float3 C", rc->final0 >> 8, 0); input(&w, "float3 D", rc->final0, 0);
        input(&w, "float G", rc->final1 >> 8, 1);
        emit(&w, "return saturate(float4(D+C*(1-A)+B*A,G));}\n");
    } else emit(&w, "return saturate(r12);}\n");
    return w.failed ? -1 : (int)w.length;
}
