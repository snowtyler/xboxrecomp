/*
 * The kernel bridge.
 *
 * A title does not call the kernel by name. Its indirect calls land on a
 * thunk table that xbox_kernel_bridge_init() has rewritten to synthetic VAs,
 * and kernel_thunk_dispatch turns each of those into a bridge_* function.
 * That is the path the runtime actually executes, and it is easy to leave
 * untested: bridge_* functions are static and read guest CPU state rather
 * than taking arguments, so they cannot be called directly.
 *
 * They are reachable all the same. recomp_lookup_kernel is not static: given
 * a buffer standing in for guest memory it selects the slot and hands back
 * the dispatcher. tests/kernel_directory uses the same seam for the directory
 * ABI; this one covers kernel memory ordinals.
 *
 * Checked here with ordinal 173, MmGetPhysicalAddress, which had two
 * implementations that disagreed -- the bridge translating through the
 * contiguous window and xbox_MmGetPhysicalAddress returning its argument
 * unchanged. The bridge now calls the latter, and that delegation is what
 * these checks pin: asserting only on xbox_MmGetPhysicalAddress leaves a
 * bridge that stops delegating entirely green.
 */
/* kernel.h brings the NT type vocabulary: <windows.h> on Windows, the
 * shim in platform/xbox_winnt.h elsewhere. Nothing below is
 * platform-specific. */
#include "kernel.h"
#include "xbox_memory_layout.h"   /* RECOMP_TLS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provided by the generated title; a regression harness has no title, and a
 * static archive resolves only what gets pulled in. Nothing here calls a
 * guest function. */
typedef void (*recomp_func_t)(void);
static void callback(void);
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { return xbox_va == 0x123400 ? callback : NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

/* The seam, plus the guest CPU state the dispatcher reads and writes. */
extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern RECOMP_TLS uint32_t g_ecx, g_edx, g_ebx, g_ebp, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;

static int failures;
static unsigned callback_args, callback_calls, callback_cdecl;
static void callback(void)
{
    callback_calls++;
    g_eax=0x13579; g_ecx=0xBAD; g_edx=0xBAD; g_ebx=0xBAD;
    g_ebp=0xBAD; g_esi=0xBAD; g_edi=0xBAD;
    /* Model either stdcall RET n or cdecl RET, deliberately clobbering all
     * registers to exercise restoration rather than relying on guest ABI. */
    g_esp += 4 + (callback_cdecl ? 0 : 4 * callback_args);
}

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-58s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const uint32_t THUNK_VA = 0x10000, STACK_VA = 0x20000;

    uint8_t *mem = VirtualAlloc(NULL, 16 * 1024 * 1024,
                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(mem != NULL, "guest memory stand-in allocated", NULL);
    if (!mem) {
        printf("\n%d failure(s); the rest needs guest memory.\n", failures);
        return 1;
    }
    g_xbox_mem_offset = (ptrdiff_t)mem;

    /* One import, in the form an XBE stores it: 0x80000000 | ordinal. */
    *(uint32_t *)(mem + THUNK_VA) = 0x80000000u | 173u;
    *(uint32_t *)(mem + THUNK_VA + 4) = 0x80000000u | 232u;
    *(uint32_t *)(mem + THUNK_VA + 8) = 0x80000000u | 153u;
    *(uint32_t *)(mem + THUNK_VA + 12) = 0x80000000u | 219u;
    xbox_kernel_set_thunk_address(THUNK_VA, 4);
    xbox_kernel_bridge_init();

    /* bridge_init rewrites the entry in place to a synthetic VA. */
    recomp_func_t fn = recomp_lookup_kernel(*(uint32_t *)(mem + THUNK_VA));
    check(fn != NULL, "ordinal 173 resolves to a bridge entry point",
          "the thunk entry was not rewritten to a synthetic VA");

    if (fn) {
        /* stdcall: the guest pushed a return address, then the argument. */
        uint32_t *sp = (uint32_t *)(mem + STACK_VA);
        sp[0] = 0xBEEF0001u;          /* guest return address */
        sp[1] = 0x80001000u;          /* inside the contiguous window */
        g_esp = STACK_VA;
        g_eax = 0xDEADBEEFu;
        fn();

        char detail[128];
        snprintf(detail, sizeof detail,
                 "bridge returned 0x%08X, expected 0x00001000", g_eax);
        check(g_eax == 0x00001000u,
              "ordinal 173 translates through the contiguous window", detail);

        /* And the argument must come off the guest stack. A wrong
         * stdcall_args_for_ordinal entry corrupts the caller's frame away
         * from the call, with nothing naming the ordinal. */
        snprintf(detail, sizeof detail, "esp is 0x%08X, expected 0x%08X",
                 g_esp, STACK_VA + 8);
        check(g_esp == STACK_VA + 8,
              "ordinal 173 pops its stdcall argument", detail);
    }

    for (unsigned caller_cleanup = 0; caller_cleanup < 2; ++caller_cleanup) {
        for (unsigned slot = 1; slot <= 3; ++slot) {
            uint32_t *sp = (uint32_t *)(mem + STACK_VA);
            memset(sp, 0, 64);
            unsigned outer_args = slot == 3 ? 8 : 3;
            callback_args = slot == 2 ? 1 : 3;
            callback_cdecl = caller_cleanup;
            sp[0] = 0xDEADBEEF;
            if (slot == 1) { sp[1]=0x123400; sp[2]=0x30000; }
            if (slot == 2) { sp[2]=0x123400; sp[3]=0xA55A; }
            if (slot == 3) {
                /* Invalid handle still completes the I/O and dispatches its
                 * APC through deliver_one_apc, without needing a host file. */
                sp[3]=0x123400; sp[4]=0xA55A; sp[5]=0x30000;
                sp[6]=0x31000; sp[7]=1;
            }
            sp[outer_args+1] = 0xCAFEBABE; /* caller's saved ESI */
            g_eax=1; g_ecx=2; g_edx=3; g_ebx=4; g_ebp=5; g_esi=6; g_edi=7;
            g_esp=STACK_VA;
            unsigned before=callback_calls;
            recomp_lookup_kernel(*(uint32_t *)(mem+THUNK_VA+4*slot))();
            check(callback_calls==before+1, "callback invoked through actual kernel thunk", NULL);
            check(g_ecx==2 && g_edx==3 && g_ebx==4 && g_ebp==5 && g_esi==6 && g_edi==7,
                  "callback restores caller registers", NULL);
            check(g_esp==STACK_VA+4*(outer_args+1), "callback restores stack before outer stdcall cleanup", NULL);
            if (slot==2) check(g_eax==0x13579, "synchronize returns callback EAX", NULL);
            if (g_esp==STACK_VA+4*(outer_args+1)) {
                g_esi=*(uint32_t *)(mem+g_esp); g_esp+=4; /* POP ESI */
                check(g_esi==0xCAFEBABE, "caller POP ESI reads saved value after APC", NULL);
            }
        }
    }
    VirtualFree(mem, 0, MEM_RELEASE);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
