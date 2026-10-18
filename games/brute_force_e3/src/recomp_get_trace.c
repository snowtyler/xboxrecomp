/* Optional observations of the SDK wait protocol. Never changes guest state. */
#include "recomp/gen/recomp_types.h"
#include "nv2a_order_trace.h"
#include "platform/xbox_winnt.h"
#include <stdlib.h>
static RECOMP_TLS uint32_t s_wait_serial;
static RECOMP_TLS uint64_t s_begin,s_poll;
static RECOMP_TLS uint32_t s_path;
static int enabled(void)
{
    static int value=-1;
    if(value<0)value=getenv("RECOMP_NV2A_GET_MONITOR")!=NULL;
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
