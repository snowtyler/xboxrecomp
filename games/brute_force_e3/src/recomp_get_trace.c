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
/* Optional cumulative per-thread observations. QPC is wall time; CPU cycles
 * exclude descheduling. No per-iteration hook is added to the busy loop. */
typedef struct WaitProfile {
    uint64_t stage_cpu, stage_qpc, last_report;
    uint64_t completed, poll_count, event_count, poll_qpc, event_qpc;
    uint64_t poll_cpu, event_cpu, cpu_errors;
    int active, stage, cpu_valid;
} WaitProfile;
static RECOMP_TLS WaitProfile s_profile;
static int profile_enabled(void)
{
    static int value=-1;
    if(value<0)value=nv2a_get_flag("RECOMP_BRUTE_FORCE_WAIT_PROFILE",0);
    return value;
}
static int thread_cpu(uint64_t *result)
{
    ULONG64 cycles=0;
    if(!QueryThreadCycleTime(GetCurrentThread(),&cycles))return 0;
    *result=cycles;return 1;
}
static int thread_time(uint64_t *result)
{
    FILETIME created,exited,kernel,user;
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;
    *result=(((uint64_t)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime)+
            (((uint64_t)user.dwHighDateTime<<32)|user.dwLowDateTime);
    return 1;
}
static void profile_stage(int stage)
{
    if(!profile_enabled()||!s_profile.active||s_profile.stage)return;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);
    s_profile.stage=stage;s_profile.stage_qpc=(uint64_t)q.QuadPart;
    s_profile.cpu_valid=thread_cpu(&s_profile.stage_cpu);
}
static int enabled(void)
{
    static int value=-1;
    if(value<0)value=nv2a_get_monitor_enabled()||profile_enabled();
    return value;
}
void brute_force_get_wait_begin(uint32_t serial,uint32_t flags,uint32_t stack)
{
    if(!enabled())return;
    s_wait_serial=serial;
    LARGE_INTEGER q;QueryPerformanceCounter(&q);s_begin=(uint64_t)q.QuadPart;s_poll=0;s_path=0;
    if(profile_enabled()) {
        s_profile.active=1;s_profile.stage=0;
        if(!s_profile.last_report)s_profile.last_report=s_begin;
    }
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
    profile_stage(1);
    nv2a_order_trace(NV_ORDER_WAIT_POLL,s_wait_serial,0,0,0);
}
void brute_force_get_wait_event(void) {profile_stage(2);}
void brute_force_get_wait_end(void)
{
    if(!enabled())return;
    LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    uint64_t us=((uint64_t)q.QuadPart-s_begin)*1000000u/(uint64_t)f.QuadPart;
    uint64_t spin=s_poll?((uint64_t)q.QuadPart-s_poll)*1000000u/(uint64_t)f.QuadPart:0;
    nv2a_order_trace(NV_ORDER_WAIT_END,s_wait_serial,s_path,
        (uint32_t)(us>0xffffffffu?0xffffffffu:us),(uint32_t)(spin>0xffffffffu?0xffffffffu:spin));
    if(profile_enabled()&&s_profile.active) {
        uint64_t cpu=0;int valid=thread_cpu(&cpu);
        s_profile.completed++;
        if(s_profile.stage) {
            uint64_t wall=(uint64_t)q.QuadPart-s_profile.stage_qpc;
            if(s_profile.stage==1){s_profile.poll_count++;s_profile.poll_qpc+=wall;}
            else {s_profile.event_count++;s_profile.event_qpc+=wall;}
            if(valid&&s_profile.cpu_valid&&cpu>=s_profile.stage_cpu) {
                if(s_profile.stage==1)s_profile.poll_cpu+=cpu-s_profile.stage_cpu;
                else s_profile.event_cpu+=cpu-s_profile.stage_cpu;
            } else s_profile.cpu_errors++;
        }
        s_profile.active=0;
        if((uint64_t)q.QuadPart-s_profile.last_report>=(uint64_t)f.QuadPart*2) {
            uint64_t total_cpu=0;int total_valid=thread_time(&total_cpu);
            char cpu_text[64];if(total_valid)snprintf(cpu_text,sizeof cpu_text,"%.3f",total_cpu/10000.0);
            else snprintf(cpu_text,sizeof cpu_text,"null");
            fprintf(stderr,"[SDK WAIT PROFILE] {\"version\":2,\"tid\":%lu,\"qpc_ms\":%.3f,\"completed\":%llu,\"poll_count\":%llu,\"event_count\":%llu,\"poll_wall_ms\":%.3f,\"event_wall_ms\":%.3f,\"poll_cpu_ms\":null,\"event_cpu_ms\":null,\"poll_cycles\":%llu,\"event_cycles\":%llu,\"thread_cycles\":%llu,\"thread_cpu_ms\":%s,\"cpu_errors\":%llu}\n",
                GetCurrentThreadId(),q.QuadPart*1000.0/f.QuadPart,
                (unsigned long long)s_profile.completed,(unsigned long long)s_profile.poll_count,
                (unsigned long long)s_profile.event_count,s_profile.poll_qpc*1000.0/f.QuadPart,
                s_profile.event_qpc*1000.0/f.QuadPart,(unsigned long long)s_profile.poll_cpu,
                (unsigned long long)s_profile.event_cpu,(unsigned long long)cpu,cpu_text,
                (unsigned long long)s_profile.cpu_errors);
            s_profile.last_report=(uint64_t)q.QuadPart;
        }
    }
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
