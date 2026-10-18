/* Exercise the actual hooks with independent fake wall and thread clocks. */
#include "platform/xbox_winnt.h"
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#define RECOMP_TYPES_H
#define RECOMP_TLS __declspec(thread)
static uint32_t guest_word;
#define MEM32(a) guest_word
#define MEM8(a) (*(uint8_t *)&guest_word)
static uint64_t wall=100,cpu;
static int clock_calls,cpu_valid=1;
static BOOL clock_now(LARGE_INTEGER *q){clock_calls++;q->QuadPart=wall;return 1;}
static BOOL clock_hz(LARGE_INTEGER *q){q->QuadPart=1000000;return 1;}
static BOOL cpu_now(HANDLE h,FILETIME *a,FILETIME *b,FILETIME *k,FILETIME *u)
{
    (void)h;(void)a;(void)b;
    k->dwHighDateTime=0;k->dwLowDateTime=0;
    u->dwHighDateTime=(DWORD)(cpu>>32);u->dwLowDateTime=(DWORD)cpu;
    return cpu_valid;
}
#define QueryPerformanceCounter clock_now
#define QueryPerformanceFrequency clock_hz
#define GetThreadTimes cpu_now
#include "../../games/brute_force_e3/src/recomp_get_trace.c"
void nv2a_order_trace(uint32_t e,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{(void)e;(void)a;(void)b;(void)c;(void)d;}
void nv2a_pb_get_wait_check(uint32_t a,uint32_t b,uint32_t c)
{(void)a;(void)b;(void)c;}
int main(int argc,char **argv)
{
    (void)argv;
    _putenv_s("RECOMP_NV2A_GET_MONITOR","0");
    _putenv_s("RECOMP_BRUTE_FORCE_WAIT_PROFILE",argc>1?"0":"1");
    brute_force_get_wait_begin(1,0,0);
    if(argc>1) {
        brute_force_get_wait_poll();brute_force_get_wait_event();brute_force_get_wait_end();
        assert(!clock_calls&&!s_profile.completed);return 0;
    }
    wall=200;cpu=1000;brute_force_get_wait_poll();
    wall=900;cpu=3000;brute_force_get_wait_end();
    assert(s_profile.poll_qpc==700&&s_profile.poll_cpu==2000);
    wall=1000;brute_force_get_wait_begin(2,0,0);
    wall=1100;cpu=4000;brute_force_get_wait_event();
    wall=1200;brute_force_get_wait_event(); /* retry retains first start */
    wall=1600;brute_force_get_wait_end();
    assert(s_profile.event_qpc==500&&!s_profile.event_cpu);
    wall=1700;brute_force_get_wait_begin(3,0,0);brute_force_get_wait_end();
    assert(s_profile.completed==3&&s_profile.poll_count==1&&s_profile.event_count==1);
    wall=1800;brute_force_get_wait_begin(4,0,0);brute_force_get_wait_poll();
    cpu_valid=0;wall=1900;brute_force_get_wait_end();
    assert(s_profile.cpu_errors==1&&s_profile.poll_qpc==800);
    assert(s_profile.poll_cpu==2000); /* unknown CPU is not invented */
    return 0;
}
