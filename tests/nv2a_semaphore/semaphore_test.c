#include "kernel.h"
#include "xbox_memory_layout.h"
#include "nv2a_hw_raster.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_scan(uint32_t start, uint32_t end);
#define G(va) (*(volatile uint32_t *)((uintptr_t)(va) + xbox_GetMemoryOffset()))
static HANDLE flush_start;
static DWORD WINAPI concurrent_flush(void *parameter)
{
    WaitForSingleObject(flush_start,INFINITE);
    for(unsigned i=0;i<10000;i++) {
        G(0xFD100410)|=0x10000;
        assert((G(0xFD100410)&0x10000)==0);
        if(parameter) {
            G(0xFD100240)=i;
            assert(G(0xFD100240)==i);
        }
    }
    return 0;
}
static void vertex(float x,float y) {
    union {float f;uint32_t u;} v;
    v.f=x;nv2a_pb_exec_method(0,0x1518,v.u);
    v.f=y;nv2a_pb_exec_method(0,0x151C,v.u);
    v.f=0;nv2a_pb_exec_method(0,0x1520,v.u);
    v.f=1;nv2a_pb_exec_method(0,0x1524,v.u);
}
static void triangle(void) {
    nv2a_pb_exec_method(0,0x17FC,5);
    vertex(0,0);vertex(16,0);vertex(0,16);
    nv2a_pb_exec_method(0,0x17FC,0);
}
int main(int argc, char **argv)
{
    assert(argc==2||(argc==3&&strcmp(argv[2],"--gpu")==0));
    int gpu=argc==3;
    if(gpu) {
        _putenv_s("RECOMP_HW_RASTER","1");
        _putenv_s("RECOMP_PB_EXEC","1");
    }
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    unsigned char *xbe = malloc(n);
    assert(xbe && fread(xbe, 1, n, f) == (size_t)n); fclose(f);
    _putenv_s("RECOMP_NV2A_NOTIFY","1");
    assert(xbox_MemoryLayoutInit(xbe, n));
    xbox_Nv2aRaiseInterruptBits(0x400100,0x100001);
    assert(G(0xFD400100)==0x100001);
    G(0xFD400100)=0x100000;
    assert(G(0xFD400100)==1);
    G(0xFD400100)=0;
    assert(G(0xFD400100)==1);
    G(0xFD400704)=0x12345678;
    assert(G(0xFD400704)==0x12345678);
    xbox_Nv2aRaiseInterruptBits(0x100,0x1000);
    G(0xFD400100)=1;
    assert(G(0xFD400100)==0);
    assert(G(0xFD000100)==0);
    xbox_Nv2aRaiseInterruptBits(0x100,0x1000);
    G(0xFD000100)=0x1000;
    assert(G(0xFD000100)==0);
    xbox_Nv2aRaiseInterruptBits(0x600100,1);
    G(0xFD600100)=1;
    assert(G(0xFD600100)==0);
    /* A coherent write-combine flush is complete at the next read, without
     * waiting for a consumer-thread tick. Neighboring PFB state survives. */
    G(0xFD100240)=0x12345678;
    G(0xFD100410)=0x01010003;
    assert(G(0xFD100410)==0x01000003);
    G(0xFD100410)|=0x10000;
    assert(G(0xFD100410)==0x01000003&&G(0xFD100240)==0x12345678);
    flush_start=CreateEventA(NULL,TRUE,FALSE,NULL);
    HANDLE flush_threads[3];
    for(unsigned i=0;i<3;i++)flush_threads[i]=CreateThread(NULL,0,concurrent_flush,(void*)(uintptr_t)(i==0),0,NULL);
    SetEvent(flush_start);
    assert(WaitForMultipleObjects(3,flush_threads,TRUE,30000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<3;i++)CloseHandle(flush_threads[i]);
    CloseHandle(flush_start);
    assert(G(0xFD100410)==0x01000003);
    uint32_t slot = xbox_HeapAlloc(4096, 4096);
    uint32_t dev = slot + 64;
    uint32_t fence = xbox_ContiguousAlloc(4096, 4096);
    uint32_t surface = xbox_ContiguousAlloc(4096, 4096);
    assert(slot && fence && surface);
    G(slot) = dev; G(dev + 0x30) = fence;
    G(fence) = 0; G(fence + 4) = 0x12345678;
    assert(xbox_Nv2aBindSemaphore(8, slot, 0x30, 4) == 0);
    nv2a_pb_exec_method(0, 0x01A4, 8);
    nv2a_pb_exec_method(0, 0x1D6C, 0);
    assert(G(fence) == 0); /* Binding does not report completion. */
    G(surface) = 0x11223344;
    nv2a_pb_exec_method(0, 0x0200, 1u << 16);
    nv2a_pb_exec_method(0, 0x0204, 1u << 16);
    nv2a_pb_exec_method(0, 0x0208, 0x8); /* A8R8G8B8 */
    nv2a_pb_exec_method(0, 0x020C, 4);
    nv2a_pb_exec_method(0, 0x0210, surface - XBOX_CONTIG_BASE);
    nv2a_pb_exec_method(0, 0x1D90, 0xAABBCCDD);
    assert(G(0xFD40186C)==0xAABBCCDD);
    nv2a_pb_exec_method(0,0x1D8C,0x11223344);
    assert(G(0xFD401A88)==0x11223344);
    nv2a_pb_exec_method(0, 0x1D94, 0xF0);
    assert(G(surface) == 0xAABBCCDD && G(fence) == 0);
    xbox_Nv2aFenceBatchBegin();
    assert(xbox_Nv2aFenceBatchActive());
    nv2a_pb_exec_method(0, 0x1D70, 12);
    nv2a_pb_exec_method(0, 0x1D70, 13);
    assert(G(fence)==0); /* No command-buffer reuse before frontend completion. */
    xbox_Nv2aFenceBatchEnd(1);
    assert(!xbox_Nv2aFenceBatchActive());
    assert(G(fence) == 13); /* Prior clear has executed before completion. */
    xbox_Nv2aFenceBatchBegin();
    nv2a_pb_exec_method(0,0x1D70,14);
    assert(G(fence)==13);
    assert(xbox_Nv2aFenceBatchFlush());
    assert(G(fence)==14);
    nv2a_pb_exec_method(0,0x1D70,15);
    assert(G(fence)==14);
    xbox_Nv2aFenceBatchEnd(0);
    assert(G(fence)==14); /* A later failure does not undo executed prefix writes. */
    /* Nested frontend work still cannot acknowledge an outer unread segment.
     * A rejected nested walk also rejects the outer pending suffix. */
    xbox_Nv2aFenceBatchBegin();
    nv2a_pb_exec_method(0,0x1D70,16);
    xbox_Nv2aFenceBatchBegin();
    nv2a_pb_exec_method(0,0x1D70,17);
    xbox_Nv2aFenceBatchEnd(1);
    assert(G(fence)==14&&xbox_Nv2aFenceBatchActive());
    xbox_Nv2aFenceBatchEnd(1);
    assert(G(fence)==17&&!xbox_Nv2aFenceBatchActive());
    xbox_Nv2aFenceBatchBegin();
    nv2a_pb_exec_method(0,0x1D70,18);
    xbox_Nv2aFenceBatchBegin();xbox_Nv2aFenceBatchEnd(0);
    assert(!xbox_Nv2aFenceBatchFlush());
    xbox_Nv2aFenceBatchEnd(1);assert(G(fence)==17);
    /* A full pending list accepts coalescing, but never publishes a truncated
     * set when a 65th target arrives. The next batch must recover normally. */
    G(dev+0x38)=fence;
    assert(xbox_Nv2aBindSemaphore(12,slot,0x38,260)==0);
    for(unsigned i=0;i<65;i++)G(fence+i*4)=0xA0000000u+i;
    xbox_Nv2aFenceBatchBegin();
    for(unsigned i=0;i<64;i++)assert(xbox_Nv2aReleaseSemaphore(12,i*4,100+i));
    assert(xbox_Nv2aReleaseSemaphore(12,0,200)); /* Duplicate at capacity. */
    assert(xbox_Nv2aFenceBatchHasRelease());
    assert(!xbox_Nv2aReleaseSemaphore(12,64*4,999));
    assert(!xbox_Nv2aFenceBatchHasRelease()&&!xbox_Nv2aFenceBatchFlush());
    xbox_Nv2aFenceBatchEnd(1);
    for(unsigned i=0;i<65;i++)assert(G(fence+i*4)==0xA0000000u+i);
    xbox_Nv2aFenceBatchBegin();
    assert(xbox_Nv2aReleaseSemaphore(12,4,201));
    assert(xbox_Nv2aFenceBatchFlush());assert(G(fence+4)==201);
    assert(xbox_Nv2aReleaseSemaphore(12,8,202));
    xbox_Nv2aFenceBatchFail();assert(!xbox_Nv2aFenceBatchFlush());
    xbox_Nv2aFenceBatchEnd(1);
    assert(G(fence+4)==201&&G(fence+8)==0xA0000002u);
    assert(!xbox_Nv2aFenceBatchActive());
    nv2a_pb_exec_method(0,0x1D70,19);
    assert(G(fence)==19); /* A standalone release completes immediately. */
    G(fence)=13;
    G(fence+4)=0x12345678;
    G(surface) = 0x55667788; /* CPU writer can now safely use the surface. */
    assert(G(surface) == 0x55667788);
    assert(!xbox_Nv2aReleaseSemaphore(9, 0, 99));
    assert(!xbox_Nv2aReleaseSemaphore(8, 4, 99));
    assert(!xbox_Nv2aReleaseSemaphore(8, 1, 99));
    assert(G(fence) == 13 && G(fence + 4) == 0x12345678);
    G(slot) = 0xFFFFFFF0;
    assert(!xbox_Nv2aReleaseSemaphore(8, 0, 99));
    G(slot) = dev; G(dev + 0x30) = 0;
    assert(!xbox_Nv2aReleaseSemaphore(8, 0, 99));
    /* A valid immediate triangle proves the raster path is active. Unsetting
     * its target must not reinterpret physical zero as a writable surface. */
    nv2a_pb_exec_method(0,0x0200,16u<<16);
    nv2a_pb_exec_method(0,0x0204,16u<<16);
    nv2a_pb_exec_method(0,0x020C,64);
    nv2a_pb_exec_method(0,0x0210,surface-XBOX_CONTIG_BASE);
    for(unsigned i=0;i<4;i++)nv2a_pb_exec_method(0,0x1A30+i*4,0x3F800000);
    G(surface+20)=0x11223344;triangle();
    assert(G(surface+20)!=0x11223344);
    G(XBOX_CONTIG_BASE+20)=0xA1B2C3D4;
    nv2a_pb_exec_method(0,0x0210,0);triangle();
    assert(G(XBOX_CONTIG_BASE+20)==0xA1B2C3D4);
    /* Frame completion is INC_WRITE, including immediate presentation where
     * no STALL follows. A subsequent STALL must not count the frame twice. */
    xbox_Nv2aFrameCounterFlip(); /* suppress the fallback clock during this check */
    assert(xbox_Nv2aFrameCounter(slot,0x34)==0);
    G(dev+0x34)=0;
    nv2a_pb_exec_method(0,0x012C,0);assert(G(dev+0x34)==1);
    nv2a_pb_exec_method(0,0x0130,0);assert(G(dev+0x34)==1);
    nv2a_pb_exec_method(0,0x012C,0);assert(G(dev+0x34)==2);
    if(gpu) {
        /* Release completes real color work while the semaphore store stays
         * deferred until the frontend relinquishes unread command storage. */
        Nv2aHwState hs={0};Nv2aVshOutput vertices[4]={0};
        uint32_t indices[6]={0,1,2,2,1,3};
        G(slot)=dev;G(dev+0x30)=fence;
        nv2a_pb_exec_method(0,0x01A4,8);nv2a_pb_exec_method(0,0x1D6C,0);
        hs.surface=surface-XBOX_CONTIG_BASE;hs.width=hs.height=hs.clip_w=hs.clip_h=32;
        hs.pitch=128;hs.bpp=4;hs.depth_scale=1;
        hs.color=(unsigned char*)((uintptr_t)surface+xbox_GetMemoryOffset());
        hs.color_mask=0x01010101;hs.use_rc=1;hs.rc.final0=4;hs.rc.final1=0x1400;
        for(unsigned i=0;i<4;i++) {
            vertices[i].pos[0]=(i&1)?32:0;vertices[i].pos[1]=(i&2)?32:0;
            vertices[i].pos[3]=1;vertices[i].d0[0]=vertices[i].d0[3]=1;
            vertices[i].fog[0]=1;
        }
        for(unsigned i=0;i<1024;i++)G(surface+i*4)=0xFF0000FF;
        G(fence)=30;
        assert(nv2a_hw_draw(&hs,vertices,4,indices,6));
        xbox_Nv2aFenceBatchBegin();nv2a_pb_exec_method(0,0x1D70,31);
        assert(G(fence)==30&&G(surface+16*128+16*4)==0xFFFF0000);
        assert(nv2a_hw_complete());xbox_Nv2aFenceBatchEnd(1);
        assert(G(fence)==31&&G(surface+16*128+16*4)==0xFFFF0000);
        for(unsigned i=0;i<4;i++){vertices[i].d0[0]=0;vertices[i].d0[1]=1;}
        assert(nv2a_hw_draw(&hs,vertices,4,indices,6));
        assert(G(surface+16*128+16*4)==0xFFFF0000);
        G(slot+512)=0x41D70;G(slot+516)=32;
        nv2a_pb_scan(slot+512,slot+520);
        assert(G(fence)==32&&G(surface+16*128+16*4)==0xFF00FF00);
        nv2a_hw_shutdown();
    }
    xbox_MemoryLayoutShutdown(); free(xbe);
    puts("NV2A semaphore ordering, unset surface safety and frame completion passed");
    return 0;
}
