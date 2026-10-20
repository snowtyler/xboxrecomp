/*
 * Read-only survey of the pushbuffer a title submits.
 *
 * The title builds NV2A commands in guest RAM and advances DMA_PUT; nothing
 * here executes them, so the framebuffer stays black however far the game
 * gets. Before any of that can be made to draw, the question is what it
 * actually asks for -- which methods, on which object classes, how many of
 * them -- because that is the difference between "the existing PGRAPH
 * translator nearly covers this" and "this needs a real one".
 *
 * Purely a reader: it walks the buffer and counts, and never writes to guest
 * memory or to the GPU state. Enabled with RECOMP_PB_SCAN.
 *
 * Pushbuffer encoding (NV20/NV2A), one dword per command header:
 *   (w & 0xE0030003) == 0x00000000  increasing methods
 *   (w & 0xE0030003) == 0x40000000  non-increasing (same method, count params)
 *   (w & 0x00000003) == 0x00000001  jump
 *   (w & 0x00000003) == 0x00000002  call
 *   (w & 0xFFFF0003) == 0x00020000  return
 * For a method header: count = (w >> 18) & 0x7FF, subchannel = (w >> 13) & 7,
 * method = w & 0x1FFC.
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>
#include <string.h>
#include "nv2a_get_config.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "nv2a_hw_raster.h"
#include "nv2a_order_trace.h"
#include "nv2a_pb_scan.h"

#ifndef XBOX_CONTIG_BASE
#define XBOX_CONTIG_BASE 0x80000000u   /* physical P is at this + P */
#endif

extern ptrdiff_t xbox_GetMemoryOffset(void);
extern size_t xbox_GetMappedSize(void);
extern uint32_t xbox_ContiguousAllocatedBytes(void);
extern void xbox_Nv2aFenceBatchBegin(void);
extern int xbox_Nv2aFenceBatchHasRelease(void);
extern int xbox_Nv2aFenceBatchFailed(void);
extern void xbox_Nv2aFenceBatchFail(void);
extern void xbox_Nv2aFenceBatchEnd(int);
extern int nv2a_hw_sync(void);
extern int nv2a_hw_complete(void);

#define PB_MAX_METHODS 4096

static struct { uint32_t method, subch, count; } s_seen[PB_MAX_METHODS];
static int s_seen_count;
/* Aligned method/subchannel pairs have a small fixed hardware domain. Keep
 * their first-seen report order while avoiding a linear search per word. */
static uint16_t s_seen_slot[8][2048];
static int s_acquire_inputs=-1;

/* Parse health. An inventory is only worth reading if the walk stayed in step
 * with the command stream: a decoder that desynchronises produces plausible
 * looking method numbers out of parameter data, and the counts then describe
 * nothing. Unrecognised words are the tell. */
static uint32_t s_tot_words, s_tot_unknown, s_tot_jumps, s_tot_segments;
static uint32_t s_tot_calls;

/* Executing is opt-in separately from surveying: a survey is read-only, while
 * the executor writes to guest memory. */
extern void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param);
extern void nv2a_pb_exec_report(void);
static int s_exec_enabled = -1;

static void note(uint32_t subch, uint32_t method)
{
    uint16_t *slot=NULL;
    if(subch<8 && method<=0x1FFC && !(method&3)) {
        slot=&s_seen_slot[subch][method>>2];
        if(*slot){s_seen[*slot-1].count++;return;}
    }
    for (int i = 0; !slot && i < s_seen_count; i++) {
        if (s_seen[i].method == method && s_seen[i].subch == subch) {
            s_seen[i].count++;
            return;
        }
    }
    if (s_seen_count >= PB_MAX_METHODS) {
        /* Silently dropping past the cap is how a truncated inventory reads as
         * "the title never does that" -- exactly the wrong conclusion when the
         * inventory is being used to decide what to implement. */
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[PB] method table full at %d -- inventory is"
                            " truncated\n", PB_MAX_METHODS);
        }
    }
    if (s_seen_count < PB_MAX_METHODS) {
        s_seen[s_seen_count].method = method;
        s_seen[s_seen_count].subch  = subch;
        s_seen[s_seen_count].count  = 1;
        s_seen_count++;
        if(slot)*slot=(uint16_t)s_seen_count;
    }
}

/* NV097 (Kelvin 3D class) methods worth naming. The point of the survey is to
 * decide what a translator has to implement, and a bare method number does not
 * answer that -- "0x1808 x412" only means something once it reads
 * INLINE_ARRAY. Unnamed ones still get counted. */
static const struct { uint32_t m; const char *name; } NV097_NAMES[] = {
    { 0x0000, "SET_OBJECT" },
    { 0x0100, "NO_OPERATION" },
    { 0x0104, "SET_WARNING_ENABLE" },
    { 0x0130, "SET_FLIP_READ" },
    { 0x0200, "SET_SURFACE_CLIP_HORIZONTAL" },
    { 0x0204, "SET_SURFACE_CLIP_VERTICAL" },
    { 0x0208, "SET_SURFACE_FORMAT" },
    { 0x020C, "SET_SURFACE_PITCH" },
    { 0x0210, "SET_SURFACE_COLOR_OFFSET" },
    { 0x0214, "SET_SURFACE_ZETA_OFFSET" },
    { 0x0300, "SET_ALPHA_TEST_ENABLE" },
    { 0x0304, "SET_BLEND_ENABLE" },
    { 0x030C, "SET_DEPTH_TEST_ENABLE" },
    { 0x0310, "SET_DITHER_ENABLE" },
    { 0x0314, "SET_LIGHTING_ENABLE" },
    { 0x033C, "SET_CULL_FACE_ENABLE" },
    { 0x0340, "SET_DEPTH_MASK" },
    { 0x0350, "SET_CLEAR_DEPTH_VALUE" },
    { 0x1D8C, "SET_CLEAR_DEPTH" },
    { 0x1D90, "SET_COLOR_CLEAR_VALUE" },
    { 0x1D94, "CLEAR_SURFACE" },
    { 0x1D6C, "SET_SEMAPHORE_OFFSET" },
    { 0x1D70, "BACK_END_WRITE_SEMAPHORE_RELEASE" },
    { 0x0B80, "SET_TRANSFORM_PROGRAM" },
    { 0x0B00, "SET_TRANSFORM_CONSTANT" },
    { 0x1720, "SET_VERTEX_DATA_ARRAY_OFFSET" },
    { 0x1760, "SET_VERTEX_DATA_ARRAY_FORMAT" },
    { 0x17FC, "SET_BEGIN_END" },
    { 0x1800, "ARRAY_ELEMENT16" },
    { 0x1808, "INLINE_ARRAY" },
    { 0x1810, "DRAW_ARRAYS" },
    { 0x1B00, "SET_TEXTURE_OFFSET" },
    { 0x1B04, "SET_TEXTURE_FORMAT" },
    { 0x1B08, "SET_TEXTURE_ADDRESS" },
    { 0x1B0C, "SET_TEXTURE_CONTROL0" },
    { 0x1B14, "SET_TEXTURE_IMAGE_RECT" },
    { 0x0FD8, "SET_COMBINER_*" },
    { 0x0000, NULL },
};

static const char *nv097_name(uint32_t m)
{
    int i;
    for (i = 0; NV097_NAMES[i].name; i++)
        if (NV097_NAMES[i].m == m)
            return NV097_NAMES[i].name;
    return "";
}

void nv2a_pb_scan_report(void)
{
    int i;

    if(nv2a_get_monitor_enabled()) {
        extern Nv2aGetStats g_nv2a_get_stats;
        Nv2aGetStats *s=&g_nv2a_get_stats;
        fprintf(stderr,"[PB GET] reads=%llu far=%llu invalid=%llu untrusted=%llu max_lag=%X publications=%llu freezes=%llu backward=%llu ring_calls=%llu\n",
            (unsigned long long)s->reads,(unsigned long long)s->far_reads,
            (unsigned long long)s->invalid_get,(unsigned long long)s->untrusted_reads,s->max_lag,
            (unsigned long long)s->publications,(unsigned long long)s->freezes,
            (unsigned long long)s->backward_jumps,(unsigned long long)s->ring_calls);
        uint64_t checks,events,past;
        nv2a_pb_get_wait_stats(&checks,&events,&past);
        fprintf(stderr,"[PB WAIT] checks=%llu events=%llu read_past=%llu\n",
            (unsigned long long)checks,(unsigned long long)events,(unsigned long long)past);
    }

    if (s_exec_enabled > 0) {
        nv2a_pb_exec_report();
        fprintf(stderr, "[PB] %u pushbuffer calls followed\n", s_tot_calls);
    }
    if (!s_seen_count || !getenv("RECOMP_PB_SCAN"))
        return;
    fprintf(stderr, "[PB] %u segments, %u words, %u jumps, %u calls,"
                    " %u unrecognised -- %d distinct (subchannel, method)"
                    " pairs\n",
            s_tot_segments, s_tot_words, s_tot_jumps, s_tot_calls,
            s_tot_unknown, s_seen_count);
    for (i = 0; i < s_seen_count; i++)
        fprintf(stderr, "  [PB]   subch %u  method 0x%04X  x%-6u %s\n",
                s_seen[i].subch, s_seen[i].method, s_seen[i].count,
                nv097_name(s_seen[i].method));
    fflush(stderr);
}

/* Walk commands from va to end_va. Returns the words consumed.
 *
 * A CALL runs a subroutine -- a pushbuffer somewhere else in memory -- up to
 * its RETURN, then carries on after the CALL. This used to skip CALLs, which
 * was invisible until a title used them for real: RenderWare on the Xbox
 * compiles static geometry into pushbuffers and draws it with a CALL, so the
 * whole race track went missing from Burnout 3's main view while the
 * geometry it submits inline (environment maps, the HUD) drew fine.
 * The NV2A has one level of subroutine, so a CALL inside a call is not
 * followed. Addresses are physical, like PUT's, and reach guest memory the
 * same way: through the contiguous window. */
static int pb_readable(uint32_t va,uint32_t bytes)
{
    if(va>=XBOX_CONTIG_BASE)return (uint64_t)va+bytes<=(uint64_t)XBOX_CONTIG_BASE+0x04000000u;
    return va>=0x10000u&&(uint64_t)va+bytes<=xbox_GetMappedSize();
}
static uint32_t s_ring_low, s_ring_high, s_nop_action, s_nop_shift, s_nop_next;
static int s_ring_exact, s_get_enabled, s_get_monitor, s_get_frozen;
static uint32_t s_get_final, s_get_last;
static uint32_t (*s_get_read)(void);
static void (*s_get_publish)(uint32_t);
Nv2aGetStats g_nv2a_get_stats;

/* The SDK selects one of 64 records by (serial >> 1) & 63. A receipt binds
 * the exact release instance AND address, so wrap/reuse, completed segments
 * and unrelated subroutine reads cannot masquerade as this release. Publish
 * only AFTER the actual reserved final dword load, not get_before_read.
 * Only release/final-word observations touch these atomics; no per-word
 * atomic cursor, guest memory write, lock, allocation or trace-ring entry. */
#ifdef _WIN32
typedef int64_t GetAtomic;
static uint64_t get_load(volatile GetAtomic *p)
{return (uint64_t)InterlockedCompareExchange64(p,0,0);}
static void get_store(volatile GetAtomic *p,uint64_t v)
{InterlockedExchange64(p,(LONG64)v);}
static void get_increment(volatile GetAtomic *p){InterlockedIncrement64(p);}
#else
typedef int64_t GetAtomic;
static uint64_t get_load(volatile GetAtomic *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void get_store(volatile GetAtomic *p,uint64_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static void get_increment(volatile GetAtomic *p){__atomic_add_fetch(p,1,__ATOMIC_RELAXED);}
#endif
static volatile GetAtomic s_wait_receipts[64];
Nv2aWaitStats g_nv2a_wait_stats;
static uint32_t s_wait_end,s_wait_serial,s_wait_command;
#ifdef NV2A_PB_TEST_HOOKS
void nv2a_pb_get_wait_reset(void)
{
    for(unsigned i=0;i<64;i++)get_store(&s_wait_receipts[i],0);
    get_store(&g_nv2a_wait_stats.checks,0);get_store(&g_nv2a_wait_stats.event_decisions,0);get_store(&g_nv2a_wait_stats.event_read_past,0);
    s_wait_end=0;
}
#endif
void nv2a_pb_get_wait_stats(uint64_t *checks,uint64_t *events,uint64_t *past)
{*checks=get_load(&g_nv2a_wait_stats.checks);*events=get_load(&g_nv2a_wait_stats.event_decisions);*past=get_load(&g_nv2a_wait_stats.event_read_past);}
void nv2a_pb_get_wait_check(uint32_t serial,uint32_t command,uint32_t distance)
{
    if(!nv2a_get_monitor_enabled())return;
    get_increment(&g_nv2a_wait_stats.checks);
    if(distance<0x2000u)return; /* Correct GET may choose polling after a read. */
    get_increment(&g_nv2a_wait_stats.event_decisions);
    uint64_t identity=((uint64_t)serial<<32)|command;
    if(command && get_load(&s_wait_receipts[(serial>>1)&63])==identity)
        get_increment(&g_nv2a_wait_stats.event_read_past);
}
static void get_after_load(uint32_t va)
{
    if(s_get_monitor&&s_wait_end&&va==s_wait_end) {
        get_store(&s_wait_receipts[(s_wait_serial>>1)&63],
            ((uint64_t)s_wait_serial<<32)|s_wait_command);
        s_wait_end=0;
    }
}
#ifdef NV2A_PB_TEST_HOOKS
static void (*s_read_hook)(uint32_t,unsigned);
void nv2a_pb_bind_read_hook(void (*hook)(uint32_t,unsigned)){s_read_hook=hook;}
#define PB_READ_HOOK(va,kind) do {if(s_read_hook)s_read_hook(va,kind);} while(0)
#else
#define PB_READ_HOOK(va,kind) ((void)0)
#endif

void nv2a_pb_bind_get_io(uint32_t (*read_get)(void),void (*publish)(uint32_t))
{s_get_read=read_get;s_get_publish=publish;}
void nv2a_pb_get_submission(uint32_t final_put)
{
    static int policy_reported;
    s_get_final=final_put;s_get_frozen=0;
    s_get_enabled=nv2a_get_progress_enabled();
    s_get_monitor=nv2a_get_monitor_enabled();
    if(!policy_reported&&(s_get_enabled||s_get_monitor)) {
        policy_reported=1;
        fprintf(stderr,"[PB GET CONFIG] progress=%d monitor=%d default=%d\n",
            s_get_enabled,s_get_monitor,NV2A_GET_PROGRESS_DEFAULT);
    }
    s_get_last=s_get_read?s_get_read():0;
}
static int get_in_ring(uint32_t va){return va>=s_ring_low&&va<s_ring_high;}
static void get_freeze(uint32_t pc,unsigned reason)
{
    if(s_get_frozen)return;
    s_get_frozen=1;
    if(s_get_enabled||s_get_monitor) {
        ++g_nv2a_get_stats.freezes;
        nv2a_order_trace(NV_ORDER_GET_BOUND,pc,s_get_last,reason,0);
        fprintf(stderr,"[PB GET] frozen at %08X pc=%08X reason=%u; final policy PUT\n",s_get_last,pc,reason);
    }
}
static void get_progress(uint32_t va,int force)
{
    if(!s_get_enabled||!s_get_publish||!s_ring_exact||s_get_frozen)return;
    if(xbox_Nv2aFenceBatchFailed()){get_freeze(va,2);return;}
    if(!get_in_ring(va)||va==s_get_final||va==s_get_last)return;
    if(!force && (va<s_get_last || va-s_get_last<0x400u))return;
    s_get_publish(va);s_get_last=va;++g_nv2a_get_stats.publications;
    nv2a_order_trace(NV_ORDER_GET,va,0,0,0);
}
static void get_before_read(uint32_t va,int root)
{
    if(root)get_progress(va,0);
    if(!s_get_monitor||!s_get_read||!get_in_ring(va))return;
    ++g_nv2a_get_stats.reads;g_nv2a_get_stats.last_read=va;
    if(!s_ring_exact){++g_nv2a_get_stats.untrusted_reads;return;}
    uint32_t visible=s_get_read();
    if(!get_in_ring(visible)){++g_nv2a_get_stats.invalid_get;return;}
    uint32_t lag=va>=visible?va-visible:s_ring_high-visible+va-s_ring_low;
    if(lag>g_nv2a_get_stats.max_lag)g_nv2a_get_stats.max_lag=lag;
    if(lag>=0x1C00u) {
        if(!g_nv2a_get_stats.far_reads)nv2a_order_trace(NV_ORDER_GET_BOUND,va,visible,0,lag);
        ++g_nv2a_get_stats.far_reads;
    }
}
static uint32_t s_pending_count,s_pending_method,s_pending_subch,s_pending_noninc,s_pending_va;
static uint32_t s_last_nop;
static struct {uint32_t from,word,target;} s_branches[32];
static uint32_t s_branch_count;
static struct {uint32_t va,word;} s_headers[64];
static uint32_t s_header_count;
static void (*s_failure_trace)(void);
static void pb_reject(uint32_t *unknown)
{
    ++*unknown;
    get_freeze(0,1);
    /* A later trapped method must not publish releases from a prefix whose
     * command walk has already failed. Earlier completed prefixes stay valid. */
    if(s_exec_enabled)xbox_Nv2aFenceBatchFail();
}
void nv2a_pb_bind_failure_trace(void (*trace)(void)){s_failure_trace=trace;}
static void pb_capture_first(uint32_t root_start,uint32_t root_end,uint32_t pc,uint32_t word) {
    static int captured; const char *dump=getenv("RECOMP_PB_FAILURE_DUMP");
    if(!dump||captured++)return;
    if(s_failure_trace)s_failure_trace();
    fprintf(stderr,"[PB FIRST] root=%08X..%08X pc=%08X word=%08X\n",root_start,root_end,pc,word);
    uint32_t n=s_header_count<64?s_header_count:64;
    for(uint32_t j=s_header_count-n;j<s_header_count;j++) {
        unsigned k=j&63;fprintf(stderr,"[PB HEADER] %08X: %08X\n",s_headers[k].va,s_headers[k].word);
    }
    if(pb_readable(root_start,root_end-root_start)) {
        FILE *f=fopen(dump,"wb");if(f){fwrite((const uint8_t*)xbox_GetMemoryOffset()+root_start,1,root_end-root_start,f);fclose(f);}
    }
}
void nv2a_pb_set_ring_bounds_verified(uint32_t low,uint32_t high,int sdk_bounds)
{s_ring_low=low;s_ring_high=high;s_ring_exact=sdk_bounds&&low<high&&!(low&3)&&!(high&3);}
void nv2a_pb_set_ring_bounds(uint32_t low,uint32_t high)
{nv2a_pb_set_ring_bounds_verified(low,high,0);}
void nv2a_pb_bind_nop_record(uint32_t action,uint32_t shift,uint32_t next_offset)
{s_nop_action=action;s_nop_shift=shift;s_nop_next=next_offset;}
static uint32_t pb_walk(uint32_t va,uint32_t end_va,int ignored,uint32_t *jumps,uint32_t *unknown)
{
    const uint8_t *mem=(const uint8_t*)xbox_GetMemoryOffset();
    uint32_t root_start=va,root_end=end_va,return_va=0,words=0;int child=0;
    (void)ignored;
    /* Only an exact SDK seam may advance GET into a new lap. */
    if(s_ring_exact&&va==s_ring_low&&s_get_last>va)get_progress(va,1);
    while(va<end_va&&words<0x100000u) {
        if(!pb_readable(va,4)){pb_capture_first(root_start,root_end,va,0);fprintf(stderr,"[PB ERROR] unreadable command=%08X\n",va);pb_reject(unknown);break;}
        if(s_acquire_inputs&&!nv2a_hw_acquire_input(mem+va,4)){pb_reject(unknown);break;}
        uint32_t from=va,w=0;
        int resumed=s_pending_count&&va==s_pending_va;
        if(s_pending_count&&!resumed){static unsigned missed;if(missed++<12)fprintf(stderr,"[PB RESUME] expected=%08X got=%08X remaining=%u\n",s_pending_va,va,s_pending_count);}
        if(s_pending_count&&!resumed)get_freeze(va,5);
        if(!resumed){get_before_read(va,!child&&!return_va);PB_READ_HOOK(va,0);w=*(const uint32_t*)(mem+va);get_after_load(va);va+=4;words++; unsigned slot=s_header_count++&63;s_headers[slot].va=from;s_headers[slot].word=w;}
        if(!resumed&&((w&3)==1||(w&0xE0000003u)==0x20000000u)) {
            uint32_t physical=(w&3)==1?w&0xFFFFFFFCu:w&0x1FFFFFFCu;
            uint32_t target=XBOX_CONTIG_BASE|(physical&0x0FFFFFFFu);(*jumps)++;
            unsigned slot=s_branch_count++&31;s_branches[slot].from=from;s_branches[slot].word=w;s_branches[slot].target=target;
            if(target>=s_ring_low&&target<=s_ring_high) {
                if(target<root_start||target>root_end)break; /* wrap/future segment */
                if(!return_va) {
                    if((!child&&target<from)||(child&&target<s_get_last)) {
                        ++g_nv2a_get_stats.backward_jumps;get_freeze(from,3);
                    }
                    else get_progress(target,1);
                }
                va=target;end_va=root_end;child=0;
            } else {if(!child&&!return_va)get_progress(va,1);va=target;end_va=va+0x400000u;child=1;}
            continue;
        }
        if(!resumed&&(w&0xFFFF0003u)==0x00020000u) {
            if(!return_va){if(child)pb_reject(unknown);break;}
            va=return_va;return_va=0;end_va=root_end;child=0;continue;
        }
        if(!resumed&&(w&3)==2) {
            if(return_va){pb_capture_first(root_start,root_end,from,w);pb_reject(unknown);break;}
            uint32_t target=XBOX_CONTIG_BASE|((w&0xFFFFFFFCu)&0x0FFFFFFFu);
            if(get_in_ring(target)){++g_nv2a_get_stats.ring_calls;get_freeze(from,4);}
            get_progress(va,1);
            return_va=va;va=target;end_va=va+0x400000u;child=1;s_tot_calls++;
            static int call_trace=-1;
            if(call_trace<0)call_trace=getenv("RECOMP_PB_CALL_TRACE")!=NULL;
            if(call_trace)fprintf(stderr,"[PB CALL] from=%08X target=%08X return=%08X last_nop=%08X flags=%u\n",from,va,return_va,s_last_nop,s_last_nop&31);
            continue;
        }
        if(!resumed&&(w&0xE0030003u)!=0&&(w&0xE0030003u)!=0x40000000u){pb_capture_first(root_start,root_end,from,w);pb_reject(unknown);continue;}
        uint32_t count=resumed?s_pending_count:w>>18&0x7FF,subch=resumed?s_pending_subch:w>>13&7,method=resumed?s_pending_method:w&0x1FFC;
        int noninc=resumed?s_pending_noninc:(w&0xE0000000u)==0x40000000u;
        s_pending_count=0;
        uint32_t i;
        for(i=0;i<count&&va<end_va;i++) {
            if(!pb_readable(va,4)){pb_reject(unknown);return words;}
            if(s_acquire_inputs&&!nv2a_hw_acquire_input(mem+va,4)){pb_reject(unknown);return words;}
            get_before_read(va,!child&&!return_va);PB_READ_HOOK(va,1);
            uint32_t m=noninc?method:method+i*4,parameter=*(const uint32_t*)(mem+va);va+=4;words++;
            get_after_load(va-4);
            if(s_get_monitor&&s_ring_exact&&!child&&!return_va&&subch==0&&m==0x1D70) {
                s_wait_serial=parameter;s_wait_command=va-8;s_wait_end=s_wait_command+20;
                get_store(&s_wait_receipts[(parameter>>1)&63],0);
            }
            if(subch==0 && ((m==0x100&&parameter)||m==0x1D70))
                nv2a_order_trace(NV_ORDER_COMMAND,va-4,m,parameter,0);
            if(s_get_monitor&&subch==0&&(m==0x100||m==0x110||m==0x1D90))
                nv2a_order_trace(NV_ORDER_FETCH,va-4,m,parameter,from);
            note(subch,m);if(s_exec_enabled)nv2a_pb_exec_method(subch,m,parameter);
            if(s_get_enabled&&xbox_Nv2aFenceBatchFailed())get_freeze(va-4,2);
            if(subch==0&&m==0x100)s_last_nop=parameter;
            /* SDK 0x0039C333 emits a NOP marker, a linked-list word, then a
             * four-word CPU record. Its last word is the next GPU command.
             * This registration is explicit: other titles' NOPs are unchanged. */
            if(subch==0&&m==0x100&&s_nop_shift&&((parameter&31)==s_nop_action)) {
                uint32_t record=XBOX_CONTIG_BASE|(parameter>>s_nop_shift);
                if(record!=va+4||!pb_readable(record,s_nop_next+4)){fprintf(stderr,"[PB ERROR] bad NOP record marker=%08X pc=%08X\n",parameter,va);pb_reject(unknown);return words;}
                /* The marker is consumed, but its linked-list word and CPU
                 * record are not. Keep the fetch bound even when the marker
                 * straddles a publication stride, without entering the record. */
                if(!child&&!return_va)get_progress(va,1);
                if(s_acquire_inputs&&!nv2a_hw_acquire_input(mem+record,s_nop_next+4)){pb_reject(unknown);return words;}
                get_before_read(record+s_nop_next,0);PB_READ_HOOK(record+s_nop_next,2);
                uint32_t next=*(const uint32_t*)(mem+record+s_nop_next);
                get_after_load(record+s_nop_next);
                if((next&3)||next<record+s_nop_next+4||next>end_va){fprintf(stderr,"[PB ERROR] bad NOP continuation=%08X record=%08X\n",next,record);pb_reject(unknown);return words;}
                va=next;
            }
        }
        if(i<count&&!child){
            s_pending_count=count-i;s_pending_method=noninc?method:method+i*4;s_pending_subch=subch;s_pending_noninc=noninc;s_pending_va=va;
            fprintf(stderr,"[PB SPLIT] resume=%08X method=%04X remaining=%u\n",va,s_pending_method,s_pending_count);
        }
    }
    if(words>=0x100000u){
        pb_capture_first(root_start,root_end,va,0);
        fprintf(stderr,"[PB ERROR] command budget exhausted root=%08X..%08X pc=%08X end=%08X child=%d\n",root_start,root_end,va,end_va,child);
        static int captured;
        if(!captured++) {
            uint32_t n=s_branch_count<32?s_branch_count:32;
            for(uint32_t j=s_branch_count-n;j<s_branch_count;j++) {
                unsigned k=j&31;fprintf(stderr,"[PB HISTORY] %08X: %08X -> %08X\n",s_branches[k].from,s_branches[k].word,s_branches[k].target);
            }
            const char *dump=getenv("RECOMP_PB_BUDGET_DUMP");
            if(dump&&pb_readable(root_start,root_end-root_start)) {
                FILE *f=fopen(dump,"wb");if(f){fwrite(mem+root_start,1,root_end-root_start,f);fclose(f);}
            }
        }
        pb_reject(unknown);
    }
    return words;
}

void nv2a_pb_scan(uint32_t start_va, uint32_t end_va)
{
    uint32_t words, jumps = 0, unknown = 0;

    if (s_exec_enabled < 0)
        s_exec_enabled = getenv("RECOMP_PB_EXEC") != NULL;
    if(s_acquire_inputs<0)s_acquire_inputs=getenv("RECOMP_HW_RELEASE_COALESCE")!=NULL&&getenv("RECOMP_HW_RELEASE_BARRIER")==NULL;
    static int scan = -1;
    if (scan < 0)
        scan = getenv("RECOMP_PB_SCAN") != NULL;
    if (!(scan || s_exec_enabled) || end_va <= start_va)
        return;
    if (end_va - start_va > 0x400000u)        /* a sane single-frame bound */
        end_va = start_va + 0x400000u;

    /* Backend completion cannot let the CPU reuse command storage while this
     * synchronous frontend is still fetching the submitted stream. */
    if(s_exec_enabled){nv2a_hw_segment_begin();xbox_Nv2aFenceBatchBegin();}
    words = pb_walk(start_va, end_va, 0, &jumps, &unknown);
    /* The frontend has consumed this segment. GPU completion is required
     * before publishing a semaphore, rather than at every ring submission.
     * CPU raster/clear, target changes and presentation synchronize their
     * own backing before accessing it. */
    if(s_exec_enabled)xbox_Nv2aFenceBatchEnd(unknown==0 &&
        (!xbox_Nv2aFenceBatchHasRelease() || nv2a_hw_sync_reason(NV2A_SYNC_SEGMENT,0)));
    if(s_exec_enabled)nv2a_hw_segment_end();

    s_tot_words += words;
    s_tot_unknown += unknown;
    s_tot_jumps += jumps;
    s_tot_segments++;
}
