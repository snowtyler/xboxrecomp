#include "kernel.h"
#include "nv2a_vsh_interp.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *recomp_lookup(unsigned long va){(void)va;return NULL;}
void *recomp_lookup_manual(unsigned long va){(void)va;return NULL;}
extern void nv2a_pb_exec_method(uint32_t,uint32_t,uint32_t);
int main(int argc,char **argv)
{
    assert(argc==2);int enabled=atoi(argv[1]);
    const float values[4]={1.25f,-2.5f,3.75f,4};
    for(unsigned i=0;i<4;i++) {
        uint32_t bits;memcpy(&bits,&values[i],4);
        nv2a_pb_exec_method(0,0x09D0+4*i,bits);
        nv2a_pb_exec_method(0,0x0A50+4*i,bits);
    }
    for(unsigned i=0;i<4;i++) {
        assert(nv2a_vsh_constant(57)[i]==(enabled?values[i]:0));
        assert(nv2a_vsh_constant(56)[i]==(enabled?values[i]:0));
    }
    puts("Fog-plane and eye-position methods update the shared shader constant bank only when opted in");return 0;
}
