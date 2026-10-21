#ifndef NV2A_RENDER_DIAGNOSTICS_H
#define NV2A_RENDER_DIAGNOSTICS_H
#include <stdlib.h>
#include <string.h>

/* Startup-only diagnostics. Parse values rather than environment presence. */
static inline int nv2a_color_clamp_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("RECOMP_NV2A_COLOR_CLAMP");
        enabled = value && !strcmp(value, "1");
    }
    return enabled;
}
static inline int nv2a_v015_render_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("RECOMP_NV2A_V015_RENDER");
        enabled = value && !strcmp(value, "1");
    }
    return enabled;
}
#endif
