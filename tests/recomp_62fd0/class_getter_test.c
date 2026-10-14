#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
static unsigned registrations;
void sub_0031B979(void){assert(MEM32(esp+4)==0x0033AC80);++registrations;eax=0;esp+=4;}
void sub_00157D40(void);
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *xbe=malloc(n);
    assert(xbe&&fread(xbe,1,n,f)==(size_t)n);fclose(f);
    assert(xbox_MemoryLayoutInit(xbe,n));g_xbox_mem_offset=xbox_GetMemoryOffset();
    uint32_t stack=xbox_HeapAlloc(4096,4096)+4096-128;
    MEM32(0x484DA4)=0x10000;MEM32(0x484DA0)=0;
    for(unsigned call=0;call<2;call++){
        MEM32(stack)=0xDEADBEEF;esp=stack;
        g_ebp=g_seh_ebp=0x44444444;ebx=0x11111111;esi=0x22222222;edi=0x33333333;
        sub_00157D40();
        assert(esp==stack+4&&eax==0x484DA0);
        assert(MEM32(0x484DA0)==0xE3307376&&MEM32(0x484DA4)==0x10001);
        assert(registrations==1);
        assert(ebx==0x11111111&&esi==0x22222222&&edi==0x33333333&&g_ebp==0x44444444);
    }
    xbox_MemoryLayoutShutdown();free(xbe);
    puts("Independent class getter initializes once, registers destructor once and returns its real type object");return 0;
}
