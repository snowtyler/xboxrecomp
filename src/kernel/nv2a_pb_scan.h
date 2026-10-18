#pragma once
#include <stdint.h>

void nv2a_pb_scan(uint32_t start_va, uint32_t end_va);
void nv2a_pb_set_ring_bounds(uint32_t low, uint32_t high);
void nv2a_pb_set_ring_bounds_verified(uint32_t low, uint32_t high, int sdk_bounds);
/* The acknowledgement thread owns these callbacks and the whole submission,
 * including both scans of a wrapped submission. Addresses here are guest VAs.
 * The publisher must perform a full-barrier store. Final GET=PUT stays outside
 * the walker, after batch end. */
void nv2a_pb_bind_get_io(uint32_t (*read_get)(void), void (*publish)(uint32_t));
void nv2a_pb_get_submission(uint32_t final_put);

typedef struct {
    uint64_t reads, far_reads, invalid_get, untrusted_reads, publications;
    uint64_t freezes, backward_jumps, ring_calls;
    uint32_t max_lag, last_read;
} Nv2aGetStats;
extern Nv2aGetStats g_nv2a_get_stats;

/* O(1), title-specific SDK post-arm observation. serial is the selected
 * record's serial, command its release header address; distance is the
 * actual SDK post-arm result. No ordered trace is required. */
void nv2a_pb_get_wait_check(uint32_t serial, uint32_t command, uint32_t distance);
void nv2a_pb_get_wait_stats(uint64_t *checks, uint64_t *events, uint64_t *read_past);
typedef struct {
    volatile int64_t checks, event_decisions, event_read_past;
} Nv2aWaitStats;
extern Nv2aWaitStats g_nv2a_wait_stats;

#ifdef NV2A_PB_TEST_HOOKS
void nv2a_pb_get_wait_reset(void);
/* Runs immediately before each actual dword load, after GET publication.
 * kind: 0 header, 1 parameter, 2 SDK continuation. */
void nv2a_pb_bind_read_hook(void (*hook)(uint32_t va, unsigned kind));
#endif
