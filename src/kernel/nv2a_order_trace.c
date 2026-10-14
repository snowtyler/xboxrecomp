#include "nv2a_order_trace.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

/* Read-only external snapshots use the sequence at both ends of each record.
 * Writers are serialized; no guest/graphics lock is acquired here. Never hold
 * this lock while waiting, invoking an ISR or performing file I/O. */
#define NV_ORDER_CAPACITY 65536
typedef struct {
    uint64_t begin,qpc;
    uint32_t tid,event,a,b,c,d;
    uint64_t end;
} NvOrderRecord;
NvOrderRecord g_nv2a_order_ring[NV_ORDER_CAPACITY];
volatile LONG64 g_nv2a_order_sequence;
LARGE_INTEGER g_nv2a_order_frequency;
static INIT_ONCE s_once=INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_lock;
static int s_enabled;
static BOOL CALLBACK init(PINIT_ONCE once,PVOID parameter,PVOID *context)
{
    (void)once;(void)parameter;(void)context;
    s_enabled=getenv("RECOMP_NV2A_ORDER_TRACE")!=NULL;
    if(s_enabled) {InitializeCriticalSection(&s_lock);QueryPerformanceFrequency(&g_nv2a_order_frequency);}
    return TRUE;
}
void nv2a_order_trace(uint32_t event,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    InitOnceExecuteOnce(&s_once,init,NULL,NULL);
    if(!s_enabled)return;
    EnterCriticalSection(&s_lock);
    uint64_t sequence=(uint64_t)g_nv2a_order_sequence+1;
    NvOrderRecord *r=&g_nv2a_order_ring[(sequence-1)%NV_ORDER_CAPACITY];
    r->begin=r->end=0;MemoryBarrier();
    LARGE_INTEGER time;QueryPerformanceCounter(&time);
    r->qpc=(uint64_t)time.QuadPart;r->tid=GetCurrentThreadId();r->event=event;
    r->a=a;r->b=b;r->c=c;r->d=d;
    MemoryBarrier();r->end=sequence;MemoryBarrier();r->begin=sequence;
    InterlockedExchange64(&g_nv2a_order_sequence,(LONG64)sequence);
    LeaveCriticalSection(&s_lock);
}
#else
void nv2a_order_trace(uint32_t event,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{(void)event;(void)a;(void)b;(void)c;(void)d;}
#endif
