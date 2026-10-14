/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

void recomp_loader_trace(unsigned int address)
{
    static int enabled = -1;
    static RECOMP_TLS unsigned int slots[16], counts[16];
    unsigned int i, source;
    if (enabled < 0) enabled = getenv("RECOMP_LOADER_TRACE") != NULL;
    if (!enabled) return;
    for (i = 0; i < 16; ++i) {
        if (!slots[i]) slots[i] = address;
        if (slots[i] == address) break;
    }
    if (i == 16 || counts[i]++ >= 12) return;
    source = MEM32(g_esp + 4);
    fprintf(stderr, "[LOADER] %08X esp %08X caller %08X ecx %08X eax %08X "
            "args %08X %08X %08X %08X\n", address, g_esp, MEM32(g_esp),
            g_ecx, g_eax, source, MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16));
    if (source >= 0x10000 && source < 0x04000000)
        fprintf(stderr, "[LOADER] source %08X: %08X %08X %08X %08X %08X %08X\n",
                source, MEM32(source), MEM32(source + 4), MEM32(source + 8),
                MEM32(source + 12), MEM32(source + 16), MEM32(source + 20));
}


/* ── Register state (defined in xbox_memory_layout.c) ──────── */


/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
extern void sub_000FFF70(void);
extern void sub_00258DB0(void);
extern void sub_003ADAFC(void);
static void trace_input_report(void)
{
    uint32_t object = g_ecx;
    static int enabled = -1;
    static RECOMP_TLS uint32_t previous;
    static RECOMP_TLS unsigned reports;
    if (enabled < 0) enabled = getenv("RECOMP_INPUT_TRACE") != NULL;
    sub_003ADAFC();
    if (enabled && reports < 80) {
        uint32_t hash = 2166136261u, i;
        for (i = 0; i < 18; ++i)
            hash = (hash ^ MEM8(object + 0x14 + i)) * 16777619u;
        if (hash != previous) {
            previous = hash;
            ++reports;
            fprintf(stderr, "[INPUT] object %08X digital %04X A %u RT %u left %d/%d right %d/%d\n",
                    object, MEM16(object + 0x14), MEM8(object + 0x16),
                    MEM8(object + 0x1D), (int16_t)MEM16(object + 0x1E),
                    (int16_t)MEM16(object + 0x20), (int16_t)MEM16(object + 0x22),
                    (int16_t)MEM16(object + 0x24));
        }
    }
}
static void trace_game_alloc(void)
{
    uint32_t size = MEM32(g_esp + 4), caller = MEM32(g_esp);
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("RECOMP_LOADER_TRACE") != NULL;
    sub_00258DB0();
    if (enabled && (!g_eax || caller == 0x0006F04B)) {
        static RECOMP_TLS unsigned int reports;
        if (!g_eax || reports++ < 12)
            fprintf(stderr, "[ALLOC] size %u caller %08X result %08X pools %u max %u\n",
                    size, caller, g_eax, MEM32(0x4A9260), MEM32(0x4A925C));
        if (!g_eax) {
            static RECOMP_TLS unsigned int failures;
            if (failures++ < 5) {
                uint32_t head = MEM32(0x4A9010);
                fprintf(stderr, "[ALLOC HEAP] base %08X limit %08X head %08X next %08X\n",
                        MEM32(0x4A9008), MEM32(0x4A900C), head, MEM32(0x4A9014));
                if (head >= 0x80000000 && head < 0x88000000)
                    fprintf(stderr, "[ALLOC HEAD] %08X %08X %08X %08X %08X\n",
                            MEM32(head), MEM32(head + 4), MEM32(head + 8),
                            MEM32(head + 12), MEM32(head + 16));
            }
        }
    }
}
static void trace_menu_callback(void)
{
    uint32_t before = g_esp, saved_esi = g_esi, saved_ebx = g_ebx, saved_edi = g_edi;
    uint32_t argument = MEM32(before + 4);
    sub_000FFF70();
    if (g_esp != before + 8 || g_esi != saved_esi ||
        g_ebx != saved_ebx || g_edi != saved_edi) {
        static RECOMP_TLS unsigned reports;
        if (reports++ < 20) {
            fprintf(stderr, "[MENU ABI] arg=%08X esp %08X -> %08X expected %08X "
                    "esi %08X -> %08X ebx %08X -> %08X edi %08X -> %08X\n",
                    argument, before, g_esp, before + 8, saved_esi, g_esi,
                    saved_ebx, g_ebx, saved_edi, g_edi);
            fflush(stderr);
        }
    }
}

extern void sub_003911E0(void);
extern void sub_00391020(void);
static void trace_movie_pixels(recomp_func_t original, uint32_t address)
{
    static int enabled = -1;
    static int order_enabled = -1;
    static RECOMP_TLS unsigned order_calls;
    static RECOMP_TLS unsigned calls;
    uint32_t dest = 0, source = 0, input = 0, blocks = 0;
    int report;
    int order_report = 0;
    uint32_t order_dest = 0;
    if (enabled < 0) enabled = getenv("RECOMP_MOVIE_PIXELS") != NULL;
    if (order_enabled < 0) order_enabled = getenv("RECOMP_MOVIE_ORDER") != NULL;
    if (order_enabled) {
        order_dest = MEM32(0x4EFC80);
        if (order_dest == 0xF0184000u || order_dest == 0xF02B0000u ||
            order_dest == 0xF02AEC00u || order_dest == 0xF03DAC00u)
            order_report = order_calls++ < 128;
        if (order_report)
            fprintf(stderr, "[MOVIE ORDER] cpu begin dest=%08X\n", order_dest);
    }
    report = enabled && (++calls <= 8 || calls % 10000 == 0);
    if (report) {
        dest = MEM32(0x4EFC80); source = MEM32(0x4EFC88);
        input = MEM32(source); blocks = MEM32(g_esp + 4);
    }
    original();
    if (order_report) {
        fprintf(stderr, "[MOVIE ORDER] cpu end dest=%08X pixel=%08X\n", order_dest, MEM32(order_dest));
        fflush(stderr);
    }
    if (report) {
        fprintf(stderr, "[MOVIE PIXELS] converter=%08X call=%u src=%08X input=%08X dest=%08X blocks=%u pixels=%08X,%08X,%08X,%08X alias=%08X\n",
                address, calls, source, input, dest, blocks,
                MEM32(dest), MEM32(dest + 4), MEM32(dest + 8), MEM32(dest + 12),
                dest >= 0xF0000000u && dest < 0xF8000000u ? MEM32(0x80000000u + (dest - 0xF0000000u)) : 0);
        fflush(stderr);
    }
}
static void trace_movie_3911E0(void) { trace_movie_pixels(sub_003911E0, 0x003911E0); }
static void trace_movie_391020(void) { trace_movie_pixels(sub_00391020, 0x00391020); }

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    if (xbox_va == 0x000FFF70) return trace_menu_callback;
    if (xbox_va == 0x00258DB0) return trace_game_alloc;
    if (xbox_va == 0x003ADAFC) return trace_input_report;
    if (xbox_va == 0x003911E0) return trace_movie_3911E0;
    if (xbox_va == 0x00391020) return trace_movie_391020;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * The ICALL miss path removes its dummy return address, but it cannot
 * reproduce the callee's argument cleanup or outputs. A missed target is
 * therefore a verification failure even when the next fault is elsewhere.
 */
void recomp_icall_fail_log(uint32_t va)
{
    static RECOMP_TLS uint32_t seen[64];
    static RECOMP_TLS uint64_t hits[64];
    static RECOMP_TLS unsigned count;
    unsigned slot;
    for (slot = 0; slot < count && seen[slot] != va; ++slot) {}
    if (slot == count && count < 64) {
        seen[slot] = va;
        hits[slot] = 0;
        ++count;
    }
    if (slot < 64) {
        uint64_t n = ++hits[slot];
        while (n >= 10 && n % 10 == 0) n /= 10;
        if (n != 1) return;
    }
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);
    fprintf(stderr, "  Guest registers: esp=%08X eax=%08X ecx=%08X edx=%08X\n",
            g_esp, g_eax, g_ecx, g_edx);
    if (g_esp >= 0x10000 && g_esp < 0x04000000 - 64) {
        for (unsigned i = 0; i < 16; ++i)
            fprintf(stderr, "    stack %08X: %08X\n", g_esp + i * 4,
                    MEM32(g_esp + i * 4));
    }

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    if (g_esp >= 0x10000 && g_esp < 0x08000000u - 24) {
        fprintf(stderr, "  guest caller %08X esp %08X ecx %08X edx %08X esi %08X edi %08X\n",
                MEM32(g_esp), g_esp, g_ecx, g_edx, g_esi, g_edi);
        fprintf(stderr, "  arguments %08X %08X %08X %08X\n",
                MEM32(g_esp + 4), MEM32(g_esp + 8),
                MEM32(g_esp + 12), MEM32(g_esp + 16));
    }
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static RECOMP_TLS uint32_t seen[128];
    static RECOMP_TLS unsigned count;
    unsigned i;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    for (i = 0; i < count && seen[i] != va; ++i) {}
    if (stop || (i == count && count < 128)) {
        if (i == count && count < 128) seen[count++] = va;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
