#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "recomp/gen/recomp_types.h"

RECOMP_TLS uint32_t g_eax, g_esp, g_ebx, g_esi, g_edi, g_ebp;
ptrdiff_t g_xbox_mem_offset;
extern void sub_00330100(void);

static void check(uint32_t cache, uint32_t flags, uint32_t expected_cache,
                  uint32_t expected_enabled)
{
    g_esp = 0x10000;
    MEM32(g_esp) = 0x12345678;
    MEM32(g_esp + 4) = flags;
    MEM32(0x43F96C) = cache;
    MEM32(0x4DA604) = 0xDEADBEEF;
    g_ebx = 0x11223344; g_esi = 0x55667788;
    g_edi = 0x99AABBCC; g_ebp = 0xDDEEFF00;
    sub_00330100();
    assert(g_esp == 0x10004 && MEM32(g_esp) == flags);
    assert(g_eax == expected_cache && MEM32(0x43F96C) == expected_cache);
    assert(MEM32(0x4DA604) == expected_enabled);
    assert(g_ebx == 0x11223344 && g_esi == 0x55667788);
    assert(g_edi == 0x99AABBCC && g_ebp == 0xDDEEFF00);
}

int main(void)
{
    void *ram = calloc(1, 0x500000);
    assert(ram);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    check(0xFFFFFFFF, 0x80080003, 1, 1);
    check(1, 0x40000, 1, 0);
    check(1, 0, 1, 1);
    check(0, 0, 0, 0);
    free(ram);
    puts("Xbox MMX probe and calling convention passed");
    return 0;
}
