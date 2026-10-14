#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../../src/kernel/bridge_guest_handles.h"
static BridgeHandleTable table;
int main(void)
{
    HANDLE a = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE b = CreateEvent(NULL, TRUE, FALSE, NULL);
    uint32_t ta = bridge_handles_register(&table, a);
    BridgeHandleLease first = bridge_handles_acquire(&table, ta);
    BridgeHandleLease second = bridge_handles_acquire(&table, ta);
    assert(a && b && ta && first.handle == a && second.handle == a);
    assert(bridge_handles_register(&table, a) == ta);
    assert(bridge_handles_close(&table, ta) == NULL);
    assert(bridge_handles_resolve(&table, ta) == NULL);
    assert(bridge_handles_acquire(&table, ta).handle == NULL);
    assert(bridge_handles_close(&table, ta) == NULL);
    uint32_t tb = bridge_handles_register(&table, b);
    assert(tb && tb != ta); /* Pending close cannot recycle its slot. */
    assert(SetEvent(a));
    assert(WaitForSingleObject(first.handle, 0) == WAIT_OBJECT_0);
    assert(bridge_handles_release(&table, first) == NULL);
    assert(WaitForSingleObject(second.handle, 0) == WAIT_OBJECT_0);
    HANDLE owned = bridge_handles_release(&table, second);
    assert(owned == a && CloseHandle(owned));
    assert(bridge_handles_close(&table, ta) == NULL);
    owned = bridge_handles_close(&table, tb);
    assert(owned == b && CloseHandle(owned));
    assert(bridge_handles_acquire(&table, 0x48004000u).handle == NULL);
    puts("Handle lifetime tests passed");
    return 0;
}
