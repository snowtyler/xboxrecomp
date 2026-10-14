/* Guest tokens own native handles. Active waits keep a closed token's native
 * object alive until their last reference is released. Include after HANDLE,
 * LONG and the platform interlocked primitives have been declared. */
#ifndef BRIDGE_GUEST_HANDLES_H
#define BRIDGE_GUEST_HANDLES_H
#define BRIDGE_HANDLE_TAG 0x48000000u
#define BRIDGE_HANDLE_MASK 0x00FFFFFFu
#define BRIDGE_HANDLE_MAX 16384
typedef struct { HANDLE handle; unsigned refs; int open; } BridgeHandleSlot;
typedef struct { BridgeHandleSlot slots[BRIDGE_HANDLE_MAX]; volatile LONG lock; } BridgeHandleTable;
typedef struct { HANDLE handle; unsigned index; } BridgeHandleLease;
static void bridge_handles_lock(BridgeHandleTable *t)
{
    while (InterlockedCompareExchange(&t->lock, 1, 0)) Sleep(0);
}
static void bridge_handles_unlock(BridgeHandleTable *t) { InterlockedExchange(&t->lock, 0); }
static unsigned bridge_handles_index(uint32_t token)
{
    unsigned i = token & BRIDGE_HANDLE_MASK;
    return (token & 0xFF000000u) == BRIDGE_HANDLE_TAG && i && i < BRIDGE_HANDLE_MAX ? i : 0;
}
static uint32_t bridge_handles_register(BridgeHandleTable *t, HANDLE h)
{
    unsigned i; uint32_t token = 0;
    if (!h || h == INVALID_HANDLE_VALUE) return 0;
    bridge_handles_lock(t);
    for (i = 1; i < BRIDGE_HANDLE_MAX; ++i)
        if (t->slots[i].open && t->slots[i].handle == h) { token = BRIDGE_HANDLE_TAG | i; break; }
    if (!token) for (i = 1; i < BRIDGE_HANDLE_MAX; ++i)
        if (!t->slots[i].handle) {
            t->slots[i].handle = h; t->slots[i].open = 1;
            token = BRIDGE_HANDLE_TAG | i; break;
        }
    bridge_handles_unlock(t);
    return token;
}
static HANDLE bridge_handles_resolve(BridgeHandleTable *t, uint32_t token)
{
    unsigned i = bridge_handles_index(token); HANDLE h = NULL;
    bridge_handles_lock(t);
    if (i && t->slots[i].open) h = t->slots[i].handle;
    bridge_handles_unlock(t);
    return h;
}
static BridgeHandleLease bridge_handles_acquire(BridgeHandleTable *t, uint32_t token)
{
    BridgeHandleLease lease = { NULL, 0 }; unsigned i = bridge_handles_index(token);
    bridge_handles_lock(t);
    if (i && t->slots[i].open) {
        ++t->slots[i].refs; lease.handle = t->slots[i].handle; lease.index = i;
    }
    bridge_handles_unlock(t);
    return lease;
}
/* Return a native handle only when the caller becomes responsible for closing
 * it. NULL also means a valid close was deferred behind an active lease. */
static HANDLE bridge_handles_close(BridgeHandleTable *t, uint32_t token)
{
    unsigned i = bridge_handles_index(token); HANDLE h = NULL;
    bridge_handles_lock(t);
    if (i && t->slots[i].open) {
        t->slots[i].open = 0;
        if (!t->slots[i].refs) { h = t->slots[i].handle; t->slots[i].handle = NULL; }
    }
    bridge_handles_unlock(t);
    return h;
}
static HANDLE bridge_handles_release(BridgeHandleTable *t, BridgeHandleLease lease)
{
    HANDLE h = NULL;
    if (!lease.index) return NULL;
    bridge_handles_lock(t);
    if (--t->slots[lease.index].refs == 0 && !t->slots[lease.index].open) {
        h = t->slots[lease.index].handle; t->slots[lease.index].handle = NULL;
    }
    bridge_handles_unlock(t);
    return h;
}
#endif
