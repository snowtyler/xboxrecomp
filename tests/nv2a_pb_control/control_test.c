#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned char *memory;
ptrdiff_t xbox_GetMemoryOffset(void){return (ptrdiff_t)memory-(ptrdiff_t)0x80000000u;}
size_t xbox_GetMappedSize(void){return 0;}
uint32_t xbox_ContiguousAllocatedBytes(void){return 0x04000000u;}
static uint32_t seen[16][2],count;
static unsigned pending,syncs,releases,active,failed,observe_order,notifications,work,completed;
static int completion_ok=1;
int nv2a_hw_complete(void);
int xbox_Nv2aFenceBatchFlush(void);
void nv2a_pb_exec_method(uint32_t channel,uint32_t method,uint32_t value)
{
    assert(channel==0&&count<16);seen[count][0]=method;seen[count++][1]=value;
    if(method==0x1D70)pending=1;
    if(observe_order&&method==0x17FC)++work;
    if(observe_order&&method==0x100&&value) {
        assert(nv2a_hw_complete());
        if(xbox_Nv2aFenceBatchFlush()) {
            assert(completed==work&&pending==0);
            ++notifications;
        }
    }
}
void nv2a_pb_exec_report(void){}
void xbox_Nv2aFenceBatchBegin(void){assert(!active);active=1;pending=failed=0;}
int xbox_Nv2aFenceBatchHasRelease(void){return pending&&!failed;}
void xbox_Nv2aFenceBatchFail(void){failed=1;}
int xbox_Nv2aFenceBatchFlush(void){if(failed)return 0;if(pending)++releases;pending=0;return 1;}
void xbox_Nv2aFenceBatchEnd(int commit){if(commit&&!failed&&pending)++releases;active=0;}
int nv2a_hw_sync(void){++syncs;return 1;}
int nv2a_hw_complete(void){++syncs;if(completion_ok)completed=work;return completion_ok;}
extern void nv2a_pb_scan(uint32_t,uint32_t);
extern void nv2a_pb_set_ring_bounds(uint32_t,uint32_t);
extern void nv2a_pb_bind_nop_record(uint32_t,uint32_t,uint32_t);
#define W(offset) (*(uint32_t*)(memory+(offset)))
int main(void)
{
    memory=calloc(1,0x04000000);assert(memory);_putenv_s("RECOMP_PB_EXEC","1");
    nv2a_pb_set_ring_bounds(0x80001000,0x80001040);
    nv2a_pb_bind_nop_record(3,5,12);
    W(0x1000)=0x3001; /* Real jump into compiled geometry. */
    W(0x3000)=0x41D90;W(0x3004)=11;W(0x3008)=0x1005;
    W(0x1004)=0x40100;W(0x1008)=(0x1010u<<5)|3;
    W(0x100C)=0x3002; /* Inline CPU record is not a DMA CALL. */
    W(0x1010)=0xFFFFFFFF;W(0x1014)=0x80003000;W(0x1018)=4096;W(0x101C)=0x80001020;
    W(0x1020)=0x41D90;W(0x1024)=22;
    nv2a_pb_scan(0x80001000,0x80001028);
    assert(count==3&&seen[0][0]==0x1D90&&seen[0][1]==11);
    assert(seen[1][0]==0x100&&seen[2][0]==0x1D90&&seen[2][1]==22);
    /* Genuine CALL/RETURN still uses the one saved return address. */
    count=0;W(0x1000)=0x3002;W(0x1004)=0x41D90;W(0x1008)=44;
    W(0x3000)=0x41D90;W(0x3004)=33;W(0x3008)=0x20000;
    nv2a_pb_scan(0x80001000,0x8000100C);
    assert(count==2&&seen[0][1]==33&&seen[1][1]==44);
    /* A submitted interval can end inside a method's parameters. Payload
     * words resembling CALL/RETURN must still be consumed as parameters. */
    count=0;W(0x1000)=0xC1D90;W(0x1004)=1;W(0x1008)=0x3002;W(0x100C)=0x20000;
    nv2a_pb_scan(0x80001000,0x80001008);assert(count==1);
    nv2a_pb_scan(0x80001008,0x80001010);
    assert(count==3&&seen[1][0]==0x1D94&&seen[1][1]==0x3002&&seen[2][0]==0x1D98&&seen[2][1]==0x20000);
    count=0;W(0x1000)=0x400C1D90;
    nv2a_pb_scan(0x80001000,0x80001008);nv2a_pb_scan(0x80001008,0x80001010);
    assert(count==3&&seen[1][0]==0x1D90&&seen[2][0]==0x1D90);
    /* A wrap leaves this submitted segment; it must not scan future words. */
    count=0;W(0x1020)=0x1001;
    nv2a_pb_scan(0x80001020,0x80001024);assert(count==0);
    /* Invalid continuation is rejected before metadata can become commands. */
    count=0;W(0x1000)=0x40100;W(0x1004)=(0x100Cu<<5)|3;
    W(0x1018)=0xFFFFFFFF;nv2a_pb_scan(0x80001000,0x80001020);
    assert(count==1&&seen[0][0]==0x100);
    assert(syncs==0); /* Consuming an unfenced segment needs no GPU readback. */
    count=0;W(0x1000)=0x41D70;W(0x1004)=12;
    nv2a_pb_scan(0x80001000,0x80001008);
    assert(syncs==1&&releases==1);
    count=0;W(0x1008)=0xFFFFFFFF;
    nv2a_pb_scan(0x80001000,0x8000100C);
    assert(syncs==1&&releases==1); /* Invalid frontend cannot publish a fence. */
    /* Multiple ordered releases in one submitted segment need one final GPU
     * completion, with no publication while later draws are still unread. */
    count=0;observe_order=1;
    W(0x1000)=0x41D70;W(0x1004)=21;
    W(0x1008)=0x417FC;W(0x100C)=0;
    W(0x1010)=0x41D70;W(0x1014)=22;
    W(0x1018)=0x417FC;W(0x101C)=0;
    W(0x1020)=0x41D70;W(0x1024)=23;
    nv2a_pb_scan(0x80001000,0x80001028);
    assert(count==5&&syncs==2&&releases==2&&completed==work);
    /* A trap publishes its completed prefix before notification. A rejected
     * later suffix cannot revoke that prefix or publish a subsequent release. */
    count=0;W(0x1000)=0x417FC;W(0x1004)=0;
    W(0x1008)=0x41D70;W(0x100C)=24;
    W(0x1010)=0x40100;W(0x1014)=1; /* Ordinary nonzero NOP, no CPU record. */
    W(0x1018)=0x41D70;W(0x101C)=25;W(0x1020)=0xFFFFFFFF;
    nv2a_pb_scan(0x80001000,0x80001024);
    assert(notifications==1&&syncs==3&&releases==3);
    /* Rejection before a trap must suppress both prefix publication and ISR
     * dispatch, even though the parser can recognize later method headers. */
    count=0;W(0x1000)=0x41D70;W(0x1004)=26;
    W(0x1008)=0xFFFFFFFF;W(0x100C)=0x40100;W(0x1010)=1;
    nv2a_pb_scan(0x80001000,0x80001014);
    assert(notifications==1&&syncs==4&&releases==3);
    count=0;completion_ok=0;W(0x1000)=0x41D70;W(0x1004)=27;
    nv2a_pb_scan(0x80001000,0x80001008);
    assert(syncs==5&&releases==3); /* A failed GPU barrier acknowledges nothing. */
    count=0;completion_ok=1;nv2a_pb_scan(0x80001000,0x80001008);
    assert(syncs==6&&releases==4); /* Failure state does not poison later batches. */
    free(memory);puts("Pushbuffer control and fenced GPU synchronization passed");return 0;
}
