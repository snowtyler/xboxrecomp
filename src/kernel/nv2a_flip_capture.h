#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

/* Comma-separated absolute completed flip ordinals and inclusive ranges.
 * Validate the whole specification even after a match. -1 means malformed. */
static inline int nv2a_flip_capture_contains(const char *spec, uint32_t flip)
{
    int match = 0;
    if (!spec || !*spec) return 0;
    for (;;) {
        char *end;
        unsigned long long low, high;
        while (isspace((unsigned char)*spec)) spec++;
        if (!isdigit((unsigned char)*spec)) return -1;
        low = strtoull(spec, &end, 10); spec = end;
        if (low > UINT32_MAX) return -1;
        high = low;
        if (*spec == '-') {
            spec++;
            if (!isdigit((unsigned char)*spec)) return -1;
            high = strtoull(spec, &end, 10); spec = end;
            if (high > UINT32_MAX || high < low) return -1;
        }
        if (low <= flip && flip <= high) match = 1;
        while (isspace((unsigned char)*spec)) spec++;
        if (!*spec) return match;
        if (*spec++ != ',') return -1;
    }
}
