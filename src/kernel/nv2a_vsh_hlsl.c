#include "nv2a_vsh_hlsl.h"
#include <stdio.h>
#include <stdarg.h>
typedef struct {char *buf;size_t cap,len;int failed;} Writer;
static void emit(Writer *w,const char *format,...)
{
    if(w->failed)return;
    va_list args;va_start(args,format);int n=vsnprintf(w->buf+w->len,w->cap-w->len,format,args);va_end(args);
    if(n<0||(size_t)n>=w->cap-w->len){w->failed=1;return;}w->len+=n;
}
static uint32_t field(const uint32_t *w,int word,int bit,int width){return (w[word]>>bit)&((1u<<width)-1);}
uint32_t nv2a_vsh_input_mask(const Nv2aVshSnapshot *program)
{
    uint32_t mask=0;
    if(!program||!program->count||program->count>NV2A_VSH_SLOTS)return 0xFFFFu;
    for(uint32_t slot=0;slot<program->count;slot++) {
        const uint32_t *w=program->words[slot];
        /* Match source()'s A/B/C multiplexers, including operands that the
         * instruction may not use. The shared V index is never relative. */
        if(field(w,2,26,2)==2||field(w,2,11,2)==2||field(w,3,28,2)==2)
            mask|=1u<<field(w,1,9,4);
        if(field(w,3,0,1))return mask;
    }
    return 0xFFFFu; /* HLSL rejects a program without END; retain full gathering. */
}
static void source(Writer *out,const uint32_t *w,int which,const char *name)
{
    uint32_t neg,swizzle,mux,reg;
    if(which==0){neg=field(w,1,8,1);swizzle=field(w,1,0,8);mux=field(w,2,26,2);reg=field(w,2,28,4);}
    else if(which==1){neg=field(w,2,25,1);swizzle=field(w,2,17,8);mux=field(w,2,11,2);reg=field(w,2,13,4);}
    else{neg=field(w,2,10,1);swizzle=field(w,2,2,8);mux=field(w,3,28,2);reg=field(w,2,0,2)*4+field(w,3,30,2);}
    char ref[128],sw[5];const char *xyzw="xyzw";
    for(int i=0;i<4;i++)sw[i]=xyzw[(swizzle>>(6-i*2))&3];sw[4]=0;
    if(mux==1){if(reg==12)snprintf(ref,sizeof ref,"p");else snprintf(ref,sizeof ref,"r%u",reg<12?reg:0);}
    else if(mux==2)snprintf(ref,sizeof ref,"inputs[%u]",field(w,1,9,4));
    else{uint32_t ci=field(w,1,13,8);if(field(w,3,1,1))snprintf(ref,sizeof ref,"nc[nv_ci(%u+a0)]",ci);else snprintf(ref,sizeof ref,"nc[%u]",ci<192?ci:0);}
    emit(out,"float4 %s=%s%s.%s;\n",name,neg?"-":"",ref,sw);
}
static void masked(Writer *out,const char *dest,uint32_t mask,const char *src)
{
    char sw[5];int n=0;const char *xyzw="xyzw";
    for(int i=0;i<4;i++)if(mask&(8>>i))sw[n++]=xyzw[i];sw[n]=0;
    if(n)emit(out,"%s.%s=%s.%s;\n",dest,sw,src,sw);
}
int nv2a_vsh_hlsl(const Nv2aVshSnapshot *program,char *buffer,size_t capacity)
{
    if(!program||!program->count||program->count>NV2A_VSH_SLOTS||!buffer||!capacity)return -1;
    Writer out={buffer,capacity,0,0};
    emit(&out,"cbuffer NV_VSH_CONSTANTS:register(b2){float4 nc[192];};\n"
        "struct NvV{float4 p;float4 d0;float4 d1;float4 f;float4 t0;float4 t1;float4 t2;float4 t3;};\n"
        "int nv_ci(int i){return i>=0&&i<192?i:0;}\n"
        "float nv_rcc(float x){float v=1/x;if(abs(v)<5.42101e-20)v=v<0?-5.42101e-20:5.42101e-20;if(abs(v)>1.884467e19)v=v<0?-1.884467e19:1.884467e19;return v;}\n"
        "float4 nv_log(float x){float a=abs(x);if(a==0)return float4(asfloat(0xff800000),1,asfloat(0xff800000),1);float e=floor(log2(a));return float4(e,a/exp2(e),log2(a),1);}\n"
        "float4 nv_lit(float4 c){float nh=max(c.y,0),p=clamp(c.w,-127.9961,127.9961);float v=nh==0?(p<0?asfloat(0x7f800000):p==0?1:0):pow(nh,p);return float4(1,max(c.x,0),c.x>0?v:0,1);}\n"
        "NvV nv_vertex(float4 inputs[16]){float4 p=float4(0,0,0,1);int a0=0;\n");
    for(int i=0;i<12;i++)emit(&out,"float4 r%d=0;\n",i);
    emit(&out,"float4 o[13];for(int i=0;i<13;i++)o[i]=0;o[3].w=o[4].w=1;o[9].w=o[10].w=o[11].w=o[12].w=1;\n");
    for(uint32_t s=0;s<program->count;s++){
        const uint32_t *w=program->words[s];uint32_t mac=field(w,1,21,4),ilu=field(w,1,25,3);
        uint32_t om=field(w,3,12,4),oa=field(w,3,3,8),td=field(w,3,20,4);
        if(mac>13||(program->context_write&&om&&!field(w,3,11,1)&&oa<192))return -1;
        emit(&out,"{\n");source(&out,w,0,"A");source(&out,w,1,"B");source(&out,w,2,"C");
        const char *m="0";switch(mac){case 1:case 13:m="A";break;case 2:m="A*B";break;case 3:m="A+C";break;case 4:m="A*B+C";break;
        case 5:m="(A.x*B.x+A.y*B.y+A.z*B.z)";break;case 6:m="(A.x*B.x+A.y*B.y+A.z*B.z+B.w)";break;
        case 7:m="(A.x*B.x+A.y*B.y+A.z*B.z+A.w*B.w)";break;case 8:m="float4(1,A.y*B.y,A.z,B.w)";break;
        case 9:m="float4(A.x<B.x?A.x:B.x,A.y<B.y?A.y:B.y,A.z<B.z?A.z:B.z,A.w<B.w?A.w:B.w)";break;
        case 10:m="float4(A.x>=B.x?A.x:B.x,A.y>=B.y?A.y:B.y,A.z>=B.z?A.z:B.z,A.w>=B.w?A.w:B.w)";break;
        case 11:m="float4(A<B)";break;case 12:m="float4(A>=B)";break;}
        const char *il="0";switch(ilu){case 1:il="C";break;case 2:il="(C.x==0?asfloat(0x7f800000):1/C.x)";break;
        case 3:il="nv_rcc(C.x)";break;case 4:il="(1/sqrt(abs(C.x)))";break;
        case 5:il="float4(exp2(floor(C.x)),C.x-floor(C.x),exp2(C.x),1)";break;case 6:il="nv_log(C.x)";break;case 7:il="nv_lit(C)";break;}
        emit(&out,"precise float4 M=%s;precise float4 I=%s;\n",m,il);
        if(mac==13)emit(&out,"a0=(int)floor(A.x+0.001);\n");
        char dest[32];if(td==12)snprintf(dest,sizeof dest,"p");else snprintf(dest,sizeof dest,"r%u",td);
        if(mac&&mac!=13&&td<=12)masked(&out,dest,field(w,3,24,4),"M");
        uint32_t it=mac?1:td;if(it==12)snprintf(dest,sizeof dest,"p");else snprintf(dest,sizeof dest,"r%u",it);
        if(ilu&&it<=12)masked(&out,dest,field(w,3,16,4),"I");
        if(om&&field(w,3,11,1)&&oa<13){if(oa==0)snprintf(dest,sizeof dest,"p");else snprintf(dest,sizeof dest,"o[%u]",oa);masked(&out,dest,om,field(w,3,2,1)?"I":"M");}
        emit(&out,"}\n");
        if(field(w,3,0,1)){emit(&out,"NvV v;v.p=p;v.d0=o[3];v.d1=o[4];v.f=o[5];v.t0=o[9];v.t1=o[10];v.t2=o[11];v.t3=o[12];return v;}\n");return out.failed?-1:(int)out.len;}
    }
    return -1;
}
