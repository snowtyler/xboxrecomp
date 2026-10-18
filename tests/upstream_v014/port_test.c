#include "kernel.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void *recomp_lookup(uint32_t address) { (void)address; abort(); }
void *recomp_lookup_manual(uint32_t address) { (void)address; abort(); }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void)
{
    CHECK(xbox_VblankCount() == 0);
    uint32_t a = xbox_PortIn(0x80C0, 1), b = xbox_PortIn(0x80C0, 1);
    CHECK((a ^ b) == 0x20 && !(a & ~0x20u) && !(b & ~0x20u));
    CHECK(xbox_PortIn(0x1234, 4) == 0);
    xbox_PortOut(0x80C0, 0x55, 1);
    puts("Guest field pin alternates before vblank starts; unknown ports read zero");
    return 0;
}
