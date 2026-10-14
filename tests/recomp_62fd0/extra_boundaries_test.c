#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
void recomp_icall_fail_log(uint32_t va){(void)va;assert(0);}
void recomp_icall_not_code_log(uint32_t va){(void)va;assert(0);}
void sub_00109E40(void){eax=0;esp+=4;}
void sub_00060360(void){assert(0 && "unexpected accepted-handler path");}
void sub_00071EB0(void){assert(0 && "unexpected accepted-handler path");}
void sub_00079960(void){assert(0 && "unexpected accepted-handler path");}
void sub_00079A20(void){assert(0 && "unexpected accepted-handler path");}
void sub_00092510(void){assert(0 && "unexpected accepted-handler path");}
void sub_00097370(void){assert(0 && "unexpected accepted-handler path");}
void sub_000F8C40(void){assert(0 && "unexpected accepted-handler path");}
void sub_00100830(void){assert(0 && "unexpected accepted-handler path");}
void sub_00100A00(void){assert(0 && "unexpected accepted-handler path");}
void sub_00102D10(void){assert(0 && "unexpected accepted-handler path");}
void sub_00102DA0(void){assert(0 && "unexpected accepted-handler path");}
void sub_00102DE0(void){assert(0 && "unexpected accepted-handler path");}
void sub_00102E30(void){assert(0 && "unexpected accepted-handler path");}
void sub_00102ED0(void){assert(0 && "unexpected accepted-handler path");}
void sub_001042F0(void){assert(0 && "unexpected accepted-handler path");}
void sub_001044C0(void){assert(0 && "unexpected accepted-handler path");}
void sub_001052D0(void){assert(0 && "unexpected accepted-handler path");}
void sub_001056B0(void){assert(0 && "unexpected accepted-handler path");}
void sub_00105720(void){assert(0 && "unexpected accepted-handler path");}
void sub_0010BE90(void){assert(0 && "unexpected accepted-handler path");}
void sub_00111280(void){assert(0 && "unexpected accepted-handler path");}
void sub_00118510(void){assert(0 && "unexpected accepted-handler path");}
void sub_002546E0(void){assert(0 && "unexpected accepted-handler path");}
void sub_0031B2ED(void){assert(0 && "unexpected accepted-handler path");}
void sub_00041D90(void);
void sub_00100B60(void);
void sub_00105810(void);
void sub_001530D0(void);
void sub_00186390(void);
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *xbe=malloc(n);
    assert(xbe&&fread(xbe,1,n,f)==(size_t)n);fclose(f);
    assert(xbox_MemoryLayoutInit(xbe,n));g_xbox_mem_offset=xbox_GetMemoryOffset();
    uint32_t object=xbox_HeapAlloc(4096,4096),stack=xbox_HeapAlloc(4096,4096)+4096-128;
    struct {recomp_func_t fn;unsigned pop,kind;int al;} cases[]={
        {sub_00041D90,4,0,0},{sub_00100B60,8,1,0},
        {sub_00105810,8,2,1},{sub_001530D0,12,3,-1},
        {sub_00186390,8,4,-1},{sub_00186390,8,5,-1},{sub_00186390,8,6,-1}};
    for(unsigned test=0;test<sizeof cases/sizeof cases[0];test++){
        memset((void*)(g_xbox_mem_offset+object),0,4096);
        MEM32(stack)=0xDEADBEEF;MEM32(stack+4)=object;MEM32(stack+8)=0;
        if(cases[test].kind==0){MEM32(object+16)=object+512;MEM32(object+524)=object+1024;}
        if(cases[test].kind>=5){MEM32(object+16)=object+512;MEM32(object+516)=cases[test].kind==5?0x6E:0x70;}
        eax=0x12345678;ecx=object+64;ebx=0x11111111;esi=0x22222222;edi=0x33333333;
        g_ebp=g_seh_ebp=0x44444444;g_fp_top=0;esp=stack;
        cases[test].fn();
        assert(esp==stack+cases[test].pop);
        if(cases[test].al>=0)assert((eax&255)==(unsigned)cases[test].al);
        assert(ebx==0x11111111&&esi==0x22222222&&edi==0x33333333&&g_ebp==0x44444444);
        assert(g_fp_top==0);
    }
    xbox_MemoryLayoutShutdown();free(xbe);puts("Five merged game functions preserve stack and saved registers in seven return paths");return 0;
}
