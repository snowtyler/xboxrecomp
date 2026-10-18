#pragma once
#include <stdint.h>
/* Title objects use both ordinary and contiguous guest RAM. */
static inline int brute_force_probe_readable(uint32_t address,uint32_t bytes)
{
    uint64_t end=(uint64_t)address+bytes;
    return bytes && ((address>=0x10000u&&end<=0x04000000u)||
                     (address>=0x80000000u&&end<=0x84000000u));
}
