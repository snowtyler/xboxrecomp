#pragma once
#include <stdint.h>
enum {
    NV_ORDER_EVENT=1, NV_ORDER_IRQL, NV_ORDER_TRAP, NV_ORDER_NOTIFY,
    NV_ORDER_IRQ, NV_ORDER_PUT, NV_ORDER_COMMAND, NV_ORDER_RELEASE,
    NV_ORDER_PUBLISH, NV_ORDER_BATCH_FAIL, NV_ORDER_GET, NV_ORDER_GET_BOUND,
    NV_ORDER_WAIT_BEGIN, NV_ORDER_WAIT_CHECK, NV_ORDER_WAIT_DETAIL, NV_ORDER_FETCH,
    NV_ORDER_WAIT_POLL, NV_ORDER_WAIT_END
};
/* Diagnostic only. No guest pointers are followed by the trace writer. */
void nv2a_order_trace(uint32_t event,uint32_t a,uint32_t b,uint32_t c,uint32_t d);
