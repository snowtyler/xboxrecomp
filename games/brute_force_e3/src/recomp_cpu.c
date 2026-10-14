#include "recomp/gen/recomp_types.h"

/* Xbox MMX capability probe, replacing the pushfd/popfd/cpuid implementation
 * at 0x00330100. Report the guest CPU, rather than querying the host CPU.
 * The caller can explicitly disable the Bink MMX path with flag 0x40000.
 * This is cdecl: consume the return address, leaving the argument to caller. */
void sub_00330100(void)
{
    uint32_t flags = MEM32(g_esp + 4);
    int32_t cached = (int32_t)MEM32(0x0043F96C);
    if (cached < 0) {
        cached = 1;
        MEM32(0x0043F96C) = 1;
    }
    MEM32(0x004DA604) = cached != 0 && !(flags & 0x40000u);
    g_eax = (uint32_t)cached;
    g_esp += 4;
}
