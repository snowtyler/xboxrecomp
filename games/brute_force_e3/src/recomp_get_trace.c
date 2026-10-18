/* Optional observations of the SDK wait protocol. Never changes guest state. */
#include "recomp/gen/recomp_types.h"
#include "nv2a_order_trace.h"
#include "nv2a_get_config.h"
#include "nv2a_pb_scan.h"
#include "guest_probe_memory.h"
#include "platform/xbox_winnt.h"
#include <stdlib.h>
#include <stdio.h>
static RECOMP_TLS uint32_t s_wait_serial;
static RECOMP_TLS uint64_t s_begin,s_poll;
static RECOMP_TLS uint32_t s_path;
static int enabled(void)
{
    static int value=-1;
    if(value<0)value=nv2a_get_monitor_enabled();
    return value;
}
void brute_force_get_wait_begin(uint32_t serial,uint32_t flags,uint32_t stack)
{
    if(!enabled())return;
    s_wait_serial=serial;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);s_begin=(uint64_t)q.QuadPart;s_poll=0;s_path=0;
    nv2a_order_trace(NV_ORDER_WAIT_BEGIN,serial,flags,stack,0);
}
void brute_force_get_wait_check(uint32_t record,uint32_t device,uint32_t distance,uint32_t d2)
{
    if(!enabled())return;
    uint32_t command=MEM32(record+4),get=MEM32(MEM32(device+0x17C0)+0x44)|0x80000000u;
    s_path=distance>=0x2000u?2:1;
    nv2a_pb_get_wait_check(MEM32(record),command,distance);
    nv2a_order_trace(NV_ORDER_WAIT_CHECK,s_wait_serial,command,get,distance);
    nv2a_order_trace(NV_ORDER_WAIT_DETAIL,distance-d2,d2,distance>=0x2000u,MEM32(record));
}
void brute_force_get_wait_poll(void)
{
    if(!enabled())return;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);s_poll=(uint64_t)q.QuadPart;s_path=1;
    nv2a_order_trace(NV_ORDER_WAIT_POLL,s_wait_serial,0,0,0);
}
void brute_force_get_wait_end(void)
{
    if(!enabled())return;
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    uint64_t us=((uint64_t)q.QuadPart-s_begin)*1000000u/(uint64_t)f.QuadPart;
    uint64_t spin=s_poll?((uint64_t)q.QuadPart-s_poll)*1000000u/(uint64_t)f.QuadPart:0;
    nv2a_order_trace(NV_ORDER_WAIT_END,s_wait_serial,s_path,
        (uint32_t)(us>0xffffffffu?0xffffffffu:us),(uint32_t)(spin>0xffffffffu?0xffffffffu:spin));
}

/* These checkpoints read the actual guest request, not a menu/capture
 * heuristic. Keep preparation, UI request enqueue, consumer entry and
 * reload return distinct; a restart movie alone does not prove reload. */
void brute_force_guest_route(uint32_t pc,uint32_t request,uint32_t iface)
{
    static int on=-1;
    if(on<0)on=nv2a_get_flag("RECOMP_BRUTE_FORCE_ROUTE_STATE",0);
    if(!on||!brute_force_probe_readable(request,20)||!brute_force_probe_readable(iface,4))return;
    if((MEM8(request)&31)!=6||MEM32(iface)!=0x003B69C0u)return;
    uint32_t manager=MEM32(0x00480998),campaign=0;
    int death=-1;
    if(brute_force_probe_readable(manager,0xC38)) {
        campaign=MEM32(manager+0xC34);
        if(brute_force_probe_readable(campaign,0x28)&&MEM32(campaign+0x14)==0x159BEC67u)
            death=(MEM8(campaign+0x24)&2)!=0;
    }
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    fprintf(stderr,"[GUEST ROUTE] {\"pc\":%u,\"qpc\":%llu,\"qpc_hz\":%llu,\"request\":%u,\"interface\":%u,\"flags\":6,\"manager\":%u,\"campaign\":%u,\"death\":%d,\"payload\":[%u,%u,%u,%u,%u]}\n",
        pc,(unsigned long long)q.QuadPart,(unsigned long long)f.QuadPart,
        request,iface,manager,campaign,death,MEM32(request),MEM32(request+4),
        MEM32(request+8),MEM32(request+12),MEM32(request+16));
}

/* Reached only after the guest's mission-state, network and class checks.
 * Observe the remaining bit without executing or replacing that predicate. */
void brute_force_guest_death(uint32_t campaign)
{
    static int on=-1;
    static volatile LONG64 previous;
    if(on<0)on=nv2a_get_flag("RECOMP_BRUTE_FORCE_ROUTE_STATE",0);
    if(!on||!brute_force_probe_readable(campaign,0x28)||MEM32(campaign+0x14)!=0x159BEC67u)return;
    uint32_t flags=MEM8(campaign+0x24),predicate=(flags&2)!=0;
    LONG64 identity=((LONG64)campaign<<1)|predicate;
    if(InterlockedExchange64(&previous,identity)==identity)return;
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    fprintf(stderr,"[GUEST DEATH] {\"pc\":1121603,\"qpc\":%llu,\"qpc_hz\":%llu,\"campaign\":%u,\"flags\":%u,\"predicate\":%u}\n",
        (unsigned long long)q.QuadPart,(unsigned long long)f.QuadPart,campaign,flags,predicate);
}
