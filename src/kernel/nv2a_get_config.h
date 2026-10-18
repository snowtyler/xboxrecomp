#pragma once
#include <stdlib.h>
#include <string.h>

/* Qualified normal-barrier gameplay/death/replay. Explicit 0 is rollback;
 * monitor remains independently off by default. */
#define NV2A_GET_PROGRESS_DEFAULT 1
static inline int nv2a_get_flag(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (value && !strcmp(value, "0")) return 0;
    if (value && !strcmp(value, "1")) return 1;
    return fallback;
}
static inline int nv2a_get_progress_enabled(void)
{ return nv2a_get_flag("RECOMP_NV2A_GET_PROGRESS", NV2A_GET_PROGRESS_DEFAULT); }
static inline int nv2a_get_monitor_enabled(void)
{ return nv2a_get_flag("RECOMP_NV2A_GET_MONITOR", 0); }
