#include "kernel.h"
#include "xbox_memory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*recomp_func_t)(void);
extern RECOMP_TLS uint32_t g_eax,g_esp;
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern int xbox_TestNv2aRaiseInterrupt(void);
static volatile LONG calls;
#define G(a) (*(volatile uint32_t*)((uintptr_t)(a)+xbox_GetMemoryOffset()))
static void isr(void) {
    InterlockedIncrement(&calls);
    G(0xFD400100)=1u<<20;
    g_eax=1;g_esp+=12;
}
recomp_func_t recomp_lookup(uint32_t a){return a==0x12340?isr:NULL;}
recomp_func_t recomp_lookup_manual(uint32_t a){(void)a;return NULL;}
static DWORD WINAPI deliver(void *stack) {
    g_esp=(uint32_t)(uintptr_t)stack;
    return (DWORD)xbox_TestNv2aRaiseInterrupt();
}
int main(int argc,char **argv) {
    assert(argc==3);int gate=!strcmp(argv[2],"on");
    if(gate)_putenv_s("RECOMP_NV2A_IRQ_GATE","1");
    else _putenv_s("RECOMP_NV2A_IRQ_GATE","");
    _putenv_s("RECOMP_NV2A_NOTIFY","1");
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    unsigned char *bytes=malloc(size);assert(bytes&&fread(bytes,1,size,f)==(size_t)size);fclose(f);
    assert(xbox_MemoryLayoutInit(bytes,size));
    uint32_t mem=xbox_HeapAlloc(65536,4096);assert(mem);
    G(mem)=0x80000062; /* KeConnectInterrupt ordinal98 */
    xbox_kernel_set_thunk_address(mem,1);xbox_kernel_bridge_init();uint32_t thunk=G(mem);
    G(mem+256)=0x12340;G(mem+260)=0;G(mem+264)=3;
    g_esp=mem+4096;G(g_esp)=0;G(g_esp+4)=mem+256;recomp_lookup_kernel(thunk)();assert(g_eax==1);
    xbox_Nv2aRaiseInterruptBits(0x400100,1u<<20);
    KIRQL saved=xbox_KfRaiseIrql(2);
    HANDLE worker=CreateThread(NULL,0,deliver,(void*)(uintptr_t)(mem+32000),0,NULL);assert(worker);
    assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);DWORD status;GetExitCodeThread(worker,&status);CloseHandle(worker);
    assert((int)status==(gate?-2:1));assert(calls==(gate?0:1));
    if(gate)assert(G(0xFD400100)&(1u<<20)); /* refusal preserves pending source */
    xbox_KfLowerIrql(saved);assert(xbox_IrqlRaisedCount()==0);
    worker=CreateThread(NULL,0,deliver,(void*)(uintptr_t)(mem+32000),0,NULL);assert(worker);
    assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);GetExitCodeThread(worker,&status);CloseHandle(worker);
    assert(status==1&&calls==(gate?1:2));assert(!(G(0xFD400100)&(1u<<20)));
    printf("IRQ %s: cross-thread mask, pending source, retry and ISR restoration passed\n",argv[2]);
    xbox_MemoryLayoutShutdown();free(bytes);return 0;
}
