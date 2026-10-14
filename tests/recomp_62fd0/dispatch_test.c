#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
void recomp_icall_fail_log(uint32_t va) { (void)va; assert(0 && "unexpected indirect call"); }
void recomp_icall_not_code_log(uint32_t va) { (void)va; assert(0 && "unexpected non-code call"); }
/* Handler bodies are mocked with their original RET immediate.
 * This verifies the dispatcher ABI across every hash branch, not handlers. */
void sub_000F8C10(void) { eax=0; esp+=8; }
void sub_000F8C40(void) { eax=0; esp+=8; }
void sub_00102D10(void) { eax=0; esp+=4; }
void sub_0010BE90(void) { eax=0; esp+=4; }
void sub_0010BEB0(void) { eax=0; esp+=4; }
void sub_001114A0(void) { eax=0; esp+=4; }
void sub_00111500(void) { eax=0; esp+=4; }
void sub_00111D00(void) { eax=0; esp+=4; }
void sub_00111D50(void) { eax=0; esp+=8; }
void sub_00111DA0(void) { eax=0; esp+=8; }
void sub_00111E50(void) { eax=0; esp+=8; }
void sub_00111F00(void) { eax=0; esp+=8; }
void sub_00111FB0(void) { eax=0; esp+=8; }
void sub_00112010(void) { eax=0; esp+=8; }
void sub_00112110(void) { eax=0; esp+=12; }
void sub_001122C0(void) { eax=0; esp+=12; }
void sub_00112750(void) { eax=0; esp+=4; }
void sub_001127E0(void) { eax=0; esp+=4; }
void sub_00112840(void) { eax=0; esp+=8; }
void sub_00112900(void) { eax=0; esp+=8; }
void sub_00112A60(void) { eax=0; esp+=8; }
void sub_00112BA0(void) { eax=0; esp+=8; }
void sub_00112D70(void) { eax=0; esp+=8; }
void sub_00112EB0(void) { eax=0; esp+=8; }
void sub_001130C0(void) { eax=0; esp+=8; }
void sub_00113140(void) { eax=0; esp+=8; }
void sub_00113190(void) { eax=0; esp+=8; }
void sub_001131F0(void) { eax=0; esp+=8; }
void sub_00116070(void) { eax=0; esp+=8; }
void sub_00118510(void) { eax=0; esp+=8; }
void sub_00113570(void);
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    void *xbe=malloc(size);assert(xbe&&fread(xbe,1,size,file)==(size_t)size);fclose(file);
    assert(xbox_MemoryLayoutInit(xbe,size));g_xbox_mem_offset=xbox_GetMemoryOffset();
    uint32_t object=xbox_HeapAlloc(4096,4096),stack=xbox_HeapAlloc(4096,4096)+4096-128;
    assert(object&&stack);
    const uint32_t hashes[]={0x10B8A5CF,0x14A08C29,0x153E3B76,0x1689514D,0x194FDE78,0x1957B14B,0x1DC61127,0x2288451,0x2542B8F,0x40EBAE8,0x6044EF,0x6056D29,0x8474EEE,0x8C2555,0xE5896D33,0xE60F5016,0xEB8D434B,0xEF9AD673,0xF070A22,0xF1101A50,0xF21935F5,0xF3CD882F,0xF6209E64,0xF878A063,0xFA125125,0xFA6E46F9,0xFBE1A97,0xFC4B414A,0xFE272709,0,0xFFFFFFFF};
    for(unsigned branch=0;branch<sizeof hashes/sizeof hashes[0];branch++) {
        MEM32(stack)=0xDEADBEEF;MEM32(stack+4)=object+512;
        MEM32(object+512+0x24)=hashes[branch];
        eax=0x12345678;ecx=object;ebx=0x11111111;esi=0x22222222;edi=0x33333333;
        g_ebp=g_seh_ebp=0x44444444;esp=stack;
        sub_00113570();
        assert(esp==stack+8); /* caller return address plus ret 4 argument */
        assert((eax&255)==1);
        assert(ebx==0x11111111&&esi==0x22222222&&edi==0x33333333);
        assert(g_ebp==0x44444444);
    }
    xbox_MemoryLayoutShutdown();free(xbe);
    puts("Merged 113570 preserves stack and registers across 31 message hashes");
    return 0;
}
