#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
void recomp_icall_fail_log(uint32_t va) { (void)va; assert(0 && "unexpected indirect call"); }
void recomp_icall_not_code_log(uint32_t va) { (void)va; assert(0 && "unexpected non-code call"); }
void sub_00041430(void) { assert(0 && "unexpected acceptance handler"); }
void sub_000416F0(void) { assert(0 && "unexpected acceptance handler"); }
void sub_00049690(void) { assert(0 && "unexpected acceptance handler"); }
void sub_00097370(void) { assert(0 && "unexpected acceptance handler"); }
void sub_002546E0(void) { assert(0 && "unexpected acceptance handler"); }
void sub_000418E0(void);
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    void *xbe=malloc(size);assert(xbe&&fread(xbe,1,size,file)==(size_t)size);fclose(file);
    assert(xbox_MemoryLayoutInit(xbe,size));g_xbox_mem_offset=xbox_GetMemoryOffset();
    uint32_t object=xbox_HeapAlloc(4096,4096),stack=xbox_HeapAlloc(4096,4096)+4096-128;
    assert(object&&stack);MEM32(object+16)=object+512;MEM32(object+512+12)=object+1024;MEMF(0x3D3678)=0;
    for(unsigned branch=0;branch<2;branch++) {
        MEM32(stack)=0xDEADBEEF;MEM32(stack+4)=0;
        MEM8(object+1024+0x1A9)=branch?4:0;MEM8(object+64+0xCC)=1;MEMF(object+64+0xC8)=1;
        g_fp_top=0;
        eax=0x12345678;ecx=object+64;ebx=0x11111111;esi=0x22222222;edi=0x33333333;
        g_ebp=g_seh_ebp=0x44444444;esp=stack;
        sub_000418E0();
        assert(esp==stack+4); /* caller return address, RET with no arguments */
        assert(eax==0);assert(g_fp_top==0);
        assert(ebx==0x11111111&&esi==0x22222222&&edi==0x33333333);
        assert(g_ebp==0x44444444);
    }
    xbox_MemoryLayoutShutdown();free(xbe);
    puts("Merged 418E0 flag and float early returns preserve stack and registers");
    return 0;
}
