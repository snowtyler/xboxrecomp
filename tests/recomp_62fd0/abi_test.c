#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
void recomp_icall_fail_log(uint32_t va) { (void)va; assert(0 && "unexpected indirect call"); }
void recomp_icall_not_code_log(uint32_t va) { (void)va; assert(0 && "unexpected non-code call"); }
/* Only the unequal-argument rejection branch calls this predicate. */
void sub_001728A0(void) { eax=0; esp+=8; }
void sub_0004E3F0(void) { assert(0 && "unexpected accepted-object branch"); }
void sub_00062FD0(void);

int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    void *xbe=malloc(size);assert(xbe&&fread(xbe,1,size,file)==(size_t)size);fclose(file);
    assert(xbox_MemoryLayoutInit(xbe,size));g_xbox_mem_offset=xbox_GetMemoryOffset();
    uint32_t object=xbox_HeapAlloc(4096,4096),stack=xbox_HeapAlloc(4096,4096)+4096-128;
    assert(object&&stack);MEM32(object+0x118)=object+512;
    for(unsigned branch=0;branch<2;branch++) {
        MEM32(stack)=0xDEADBEEF;MEM32(stack+4)=object+512+branch*256;
        eax=0x12345678;ecx=object;ebx=0x11111111;esi=0x22222222;edi=0x33333333;
        g_ebp=g_seh_ebp=0x44444444;esp=stack;
        sub_00062FD0();
        assert(esp==stack+8); /* caller return address plus ret 4 argument */
        assert((eax&255)==0);
        assert(ebx==0x11111111&&esi==0x22222222&&edi==0x33333333);
        assert(g_ebp==0x44444444);
    }
    xbox_MemoryLayoutShutdown();free(xbe);
    puts("Merged 62FD0 early and predicate-rejection returns preserve stack and registers");
    return 0;
}
