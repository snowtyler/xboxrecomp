/* Deterministic SDK late-patch protocol, exercised against the real walker.
 * No scheduling or GPU timing is needed: the guest runs between actual loads. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/kernel/nv2a_pb_scan.h"
#include "../../src/kernel/nv2a_hw_raster.h"
#define BASE 0x80000000u
#define LOW  0x80002000u
#define HIGH 0x80182000u
#define START 0x80153268u
#define END 0x80158B98u
#define RECORD 0x80158B20u
#define W(a) (*(uint32_t*)(memory+((a)-BASE)))
static unsigned char *memory;
static int progress,coalesced,poison,active,failed,completion_ok=1;
static uint32_t visible,final_put,low=LOW,high=HIGH,writer;
static uint64_t stream_hash,publish_hash;
static unsigned executed,published,ended,traps,read_index,publications;
static uint32_t pending[32],npending,slot,record_address=RECORD,second_term;
static struct {uint32_t serial,address,position;} records[64];
static unsigned trigger,spread,patch_stage,guest_path,event_set;
static int guest_enabled,call_check,record_check,fail_trap;
static uint32_t record_lo,record_next,call_return;
static unsigned ring_reuse;

ptrdiff_t xbox_GetMemoryOffset(void){return (ptrdiff_t)memory-(ptrdiff_t)BASE;}
size_t xbox_GetMappedSize(void){return 0;}
uint32_t xbox_ContiguousAllocatedBytes(void){return 0x04000000u;}
static uint64_t mix(uint64_t h,uint32_t v){return (h^v)*1099511628211ull;}
int xbox_Nv2aFenceBatchFailed(void){return failed;}
void xbox_Nv2aFenceBatchBegin(void){assert(!active);active=1;failed=0;npending=0;}
void xbox_Nv2aFenceBatchFail(void){failed=1;}
int xbox_Nv2aFenceBatchHasRelease(void){return npending&&!failed;}
int xbox_Nv2aFenceBatchFlush(void)
{
    if(failed)return 0;
    for(unsigned i=0;i<npending;i++) {
        slot=pending[i];publish_hash=mix(publish_hash,slot);++published;
    }
    npending=0;return 1;
}
void xbox_Nv2aFenceBatchEnd(int commit)
{if(commit&&!failed)xbox_Nv2aFenceBatchFlush();active=0;++ended;}
int nv2a_hw_sync(void){return completion_ok;}
int nv2a_hw_complete(void){return completion_ok;}
int nv2a_hw_sync_reason(Nv2aSyncReason r,int depth)
{assert(r==NV2A_SYNC_SEGMENT&&!depth);return completion_ok;}
void nv2a_hw_segment_begin(void){}
void nv2a_hw_segment_end(void){}
int nv2a_hw_acquire_input(const void *p,size_t n){(void)p;(void)n;return 1;}
void nv2a_pb_exec_report(void){}
void nv2a_pb_exec_method(uint32_t channel,uint32_t method,uint32_t value)
{
    assert(!channel);++executed;
    stream_hash=mix(mix(stream_hash,method),value);
    if(method==0x1D70) {
        if(!coalesced&&!nv2a_hw_complete())xbox_Nv2aFenceBatchFail();
        assert(npending<32);pending[npending++]=value;
    }
    if(method==0x100&&value) {
        if(fail_trap)xbox_Nv2aFenceBatchFail();
        if(xbox_Nv2aFenceBatchFlush()) {
            ++traps;if(value==5)event_set=1;
        }
    }
}

/* C port of 0039EA90: unsigned writer comparisons, signed clamping, the
 * subroutine fallback, and the independent record-position contribution. */
static uint32_t distance(uint32_t address,uint32_t get,uint32_t fallback,
                         uint32_t position,uint32_t position_done)
{
    if(get<low||get>=high)get=(fallback&~1u)|BASE;
    uint32_t a=address,g=get,size=high-low;
    if(g<=writer)g+=size;
    if(a<=writer)a+=size;
    int32_t d1=(int32_t)(a-g);
    if(d1<0)return 0;
    int32_t d2=(int32_t)(position-position_done);
    return (uint32_t)d1+(d2>0?(uint32_t)d2:0);
}
static uint32_t read_get(void){return visible;}
static void publish_get(uint32_t va)
{
    assert(active&&!failed&&va>=low&&va<high&&va!=final_put);
    if(record_check)assert(va<=record_lo||va>=record_next);
    assert(va>=visible||va==low); /* backward publication cannot restore storage */
    if(poison) {
        uint32_t p=visible;
        while(p!=va) {
            assert(p>=low&&p<high);W(p)=0xDEADBEEFu;
            p+=4;if(p==high)p=low;
            ++ring_reuse;assert(ring_reuse<0x1000000u);
        }
    }
    visible=va;++publications;
}
static void guest_patch_step(void)
{
    switch(patch_stage++) {
    case 0:
        /* Initial serial/slot check and record lookup (the emitted serial's
         * 64-entry record is selected without an obsolete-record fallback). */
        if(slot>=0xF011u){guest_path=2;return;}
        assert(records[(0xF011u>>1)&63].serial==0xF011u);
        record_address=records[(0xF011u>>1)&63].address;
        if(distance(record_address,visible,0,second_term,0)<0x2400u){guest_path=2;return;}
        event_set=0;break;
    case 1:W(record_address+8)=0x40110;break;
    case 2:W(record_address+12)=0;break;
    case 3:W(record_address+16)=0x40100;break;
    case 4:W(record_address+20)=5;break;
    case 5:
        /* PFB flush is a full barrier in the real default trap path. */
        guest_path=distance(record_address,visible,0,second_term,0)>=0x2000u?1:2;
        nv2a_pb_get_wait_check(0xF011u,record_address,
            distance(record_address,visible,0,second_term,0));
        if(guest_path==2) {
            W(record_address+8)=W(record_address+16)=0x40100;
            W(record_address+12)=W(record_address+20)=0;
        }
        break;
    default:assert(0);
    }
}
static void read_hook(uint32_t va,unsigned kind)
{
    (void)kind;
    if(call_check&&va>=0x80300000u&&va<0x80310000u&&progress)
        assert(visible==call_return);
    if(guest_enabled&&read_index>=trigger&&!guest_path) {
        if(spread)guest_patch_step();
        else while(!guest_path)guest_patch_step();
    }
    ++read_index;
}
extern void nv2a_pb_bind_nop_record(uint32_t,uint32_t,uint32_t);
static void reset(uint32_t start,uint32_t end,int exact)
{
    nv2a_pb_get_wait_reset();
    memset(memory+(LOW-BASE),0,0x4000);
    memset(memory+(START-BASE),0,END-START);
    memset(memory+(HIGH-BASE)-0x3000,0,0x3000);
    memset(memory+0x300000,0,0x10000);
    memset(&g_nv2a_get_stats,0,sizeof(g_nv2a_get_stats));
    memset(records,0,sizeof(records));
    visible=start;final_put=end;writer=end;low=LOW;high=HIGH;
    active=failed=0;npending=0;slot=0xF00D;ended=traps=executed=published=0;
    read_index=publications=ring_reuse=0;stream_hash=publish_hash=1469598103934665603ull;
    guest_enabled=guest_path=patch_stage=event_set=0;
    call_check=record_check=fail_trap=0;completion_ok=1;
    record_address=RECORD;second_term=0;
    nv2a_pb_set_ring_bounds_verified(low,high,exact);
    nv2a_pb_bind_get_io(read_get,publish_get);
    nv2a_pb_get_submission(end);
}
static void scan(uint32_t start,uint32_t end,int final)
{
    nv2a_pb_scan(start,end);
    if(final){assert(ended&&!active);visible=final_put;}
}
static void release_at(uint32_t va,uint32_t serial)
{
    unsigned index=(serial>>1)&63;records[index].serial=serial;
    records[index].address=va;records[index].position=0;
    W(va)=0x41D70;W(va+4)=serial;
    W(va+8)=W(va+16)=0x41D90;W(va+12)=W(va+20)=0;
}
static void trace_segment(void)
{
    release_at(START+0x3E48,0xF00F);
    release_at(RECORD,0xF011);release_at(RECORD+24,0xF013);
    W(0x80158B58u)=0x40100;W(0x80158B5Cu)=0x05600141;
    release_at(START+0x5918,0xF015);
}
static unsigned replica(unsigned k,int torn,int overwrite,int require_success)
{
    poison=overwrite;reset(START,END,1);trace_segment();
    guest_enabled=1;trigger=k;spread=torn;
    scan(START,END,1);
    while(!guest_path)guest_patch_step();
    assert(slot==0xF015&&visible==END);
    if(require_success)assert(guest_path==2||(guest_path==1&&event_set));
    if(progress)assert(!g_nv2a_get_stats.far_reads&&g_nv2a_get_stats.max_lag<0x400);
    uint64_t checks,events,past;
    nv2a_pb_get_wait_stats(&checks,&events,&past);
    if(progress)assert(!past);
    return read_index;
}
static void receipt_tests(void)
{
    uint64_t checks,events,past;
    poison=0;reset(LOW,LOW+0x1000,1);release_at(LOW,0xF011);
    scan(LOW,LOW+0x1000,1);
    nv2a_pb_get_wait_check(0xF011,LOW+4,0x2000); /* wrong address */
    nv2a_pb_get_wait_check(0xF091,LOW,0x2000);   /* same slot, wrong generation */
    nv2a_pb_get_wait_check(0xF011,LOW,0);        /* correct poll decision */
    nv2a_pb_get_wait_check(0xF011,LOW,0x2000);   /* retained past segment end */
    nv2a_pb_get_wait_stats(&checks,&events,&past);
    assert(checks==4&&events==3&&past==1);
    release_at(LOW,0xF091);
    nv2a_pb_get_submission(LOW+0x1000);scan(LOW,LOW+0x1000,1);
    nv2a_pb_get_wait_check(0xF011,LOW,0x2000); /* recycled storage must not match */
    nv2a_pb_get_wait_stats(&checks,&events,&past);assert(past==1);
    nv2a_pb_get_wait_check(0xF091,LOW,0x2000);
    nv2a_pb_get_wait_stats(&checks,&events,&past);assert(past==2);
}
static void protocol_tests(void)
{
    /* With a zero-filled gap, every dword before F013 is one header load,
     * except that a release substitutes three headers plus three params. */
    unsigned k=(RECORD+24-START)/4;
    unsigned reads=replica(k,0,0,progress);
    uint64_t checks,events,past;
    nv2a_pb_get_wait_stats(&checks,&events,&past);
    if(!progress)assert(checks==1&&events==1&&past==1);
    else assert(checks<=1&&!events&&!past); /* may poll before arming */
    if(!progress)assert(guest_path==1&&!event_set&&g_nv2a_get_stats.far_reads);
    else assert(guest_path==2);
    uint64_t hash=stream_hash,pub=publish_hash;
    replica(k,0,1,progress);assert(hash==stream_hash&&pub==publish_hash);
    /* Start the guest at every possible read, with both atomic and dword-
     * interleaved patching. This includes old headers with new parameters. */
    for(unsigned torn=0;torn<2;torn++)for(unsigned point=0;point<=reads;point++) {
        replica(point,torn,0,progress);
        hash=stream_hash;pub=publish_hash;
        replica(point,torn,1,progress);
        assert(hash==stream_hash&&pub==publish_hash);
    }
    printf("SDK interleavings: %u read points, atomic/torn, reused/unreused\n",reads+1);
}
static uint64_t structural_tests(int overwrite)
{
    uint64_t result=0;poison=overwrite;
    reset(LOW,LOW+0x2100,1);
    W(LOW)=(2047u<<18)|0x40001808u;
    for(unsigned i=0;i<2047;i++)W(LOW+4+i*4)=i;
    scan(LOW,LOW+0x2100,1);assert(executed==2047);
    if(progress)assert(publications>=7&&g_nv2a_get_stats.max_lag<0x400);
    result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));

    reset(LOW,LOW+0x1000,1);call_check=1;call_return=LOW+4;
    W(LOW)=0x300002; /* CALL outside ring */
    for(unsigned i=0;i<4;i++) {
        uint32_t va=0x80300000u+i*8192;
        W(va)=(2047u<<18)|0x40001808u;
    }
    W(0x80308000u)=0x20000;
    release_at(LOW+4,1);scan(LOW,LOW+0x1000,1);
    assert(executed==4*2047+3&&published==1&&!g_nv2a_get_stats.invalid_get);
    result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));

    reset(LOW,LOW+0x1800,1);W(LOW)=(LOW+0x1000-BASE)|1;
    W(LOW+0x1000)=0x300001;W(0x80300000u)=((LOW+0x1400)-BASE)|1;
    release_at(LOW+0x1400,2);scan(LOW,LOW+0x1800,1);
    assert(published==1&&!g_nv2a_get_stats.freezes);
    result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));

    reset(LOW,LOW+0x800,1);
    W(LOW)=(1023u<<18)|0x40001808u;
    scan(LOW,LOW+0x800,1);assert(executed==511&&visible==LOW+0x800);
    /* The ack thread's actual split PUT is the payload split point. */
    final_put=LOW+0x1000;visible=LOW+0x800;
    nv2a_pb_get_submission(final_put);scan(LOW+0x800,LOW+0x1000,1);
    assert(executed==1023);
    result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));

    reset(LOW,LOW+0x1800,1);record_check=1;
    record_lo=LOW+0x400;record_next=LOW+0x1000;
    W(record_lo-12)=0x40100;W(record_lo-8)=((record_lo-BASE)<<5)|3;
    W(record_lo-4)=0x3002;W(record_lo+12)=record_next;
    release_at(record_next,3);scan(LOW,LOW+0x1800,1);
    assert(published==1&&!g_nv2a_get_stats.freezes);
    result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));

    for(unsigned exact=0;exact<2;exact++) {
        reset(HIGH-0x3000,LOW+0x800,exact);
        record_address=HIGH-0x100;release_at(record_address,0xF011);
        W(HIGH-4)=(LOW-BASE)|1;
        scan(HIGH-0x3000,HIGH,0);
        unsigned before=publications;
        scan(LOW,LOW+0x800,1);
        if(progress&&exact)assert(publications>before&&!g_nv2a_get_stats.far_reads);
        if(!exact)assert(publications==0&&g_nv2a_get_stats.untrusted_reads);
        assert(distance(record_address,LOW,0,0,0)==0);
        result=mix(mix(result,(uint32_t)(stream_hash^publish_hash)),(uint32_t)((stream_hash^publish_hash)>>32));
    }
    return result;
}
static void failure_tests(void)
{
    poison=0;
    for(unsigned failure=0;failure<4;failure++) {
        reset(LOW,LOW+0x2000,1);
        uint32_t bad=LOW+0x800;
        if(failure==0)W(bad)=0xFFFFFFFF;
        if(failure==1)W(bad)=0x7FFFFFE; /* unreadable CALL target */
        if(failure==2) {
            W(bad)=0x40100;W(bad+4)=((bad+12-BASE)<<5)|3;
            W(bad+24)=0xFFFFFFFF; /* bad SDK continuation */
        }
        if(failure==3){W(bad)=0x40100;W(bad+4)=1;fail_trap=1;}
        nv2a_pb_scan(LOW,LOW+0x2000);
        if(progress)assert(g_nv2a_get_stats.freezes==1&&visible<=bad+(failure==2?8:4));
        assert(!published);
        assert(ended&&!active);visible=final_put; /* existing failure policy */
    }
    reset(LOW,LOW+0x1000,1);W(LOW+0x800)=(LOW-BASE)|1;
    nv2a_pb_scan(LOW,LOW+0x1000);
    assert(g_nv2a_get_stats.backward_jumps);
    if(progress)assert(g_nv2a_get_stats.freezes==1);
    reset(LOW,LOW+0x1000,1);W(LOW)=(LOW+0x800-BASE)|2;W(LOW+0x800)=0x20000;
    nv2a_pb_scan(LOW,LOW+0x1000);assert(g_nv2a_get_stats.ring_calls==1);
    if(progress)assert(g_nv2a_get_stats.freezes==1&&publications==0);
}
int main(int argc,char **argv)
{
    assert(argc==3);progress=atoi(argv[1]);coalesced=atoi(argv[2]);
    _putenv_s("RECOMP_PB_EXEC","1");_putenv_s("RECOMP_NV2A_GET_MONITOR","1");
    _putenv_s("RECOMP_NV2A_GET_PROGRESS",progress?"1":"0");
    _putenv_s("RECOMP_HW_RELEASE_COALESCE",coalesced?"1":"");
    memory=calloc(1,0x04000000);assert(memory);
    nv2a_pb_bind_nop_record(3,5,12);nv2a_pb_bind_read_hook(read_hook);
    writer=HIGH;
    assert(distance(LOW+0x3000,LOW+0x1000,0,9,4)==0x2005);
    assert(distance(LOW,LOW+4,0,0,0)==0);
    assert(distance(LOW+0x3000,0,LOW+0x1001,0,0)==0x2000);
    receipt_tests();protocol_tests();
    uint64_t hash=structural_tests(0);assert(hash==structural_tests(1));
    /* Publication and executed commands must agree with the other GET arm
     * on identical immutable streams, in either completion configuration. */
    progress=!progress;_putenv_s("RECOMP_NV2A_GET_PROGRESS",progress?"1":"0");
    assert(hash==structural_tests(0));
    progress=!progress;_putenv_s("RECOMP_NV2A_GET_PROGRESS",progress?"1":"0");
    failure_tests();free(memory);
    printf("GET progress=%d coalesced=%d: protocol, storage reuse, control flow, failure checks passed\n",progress,coalesced);
    return 0;
}
