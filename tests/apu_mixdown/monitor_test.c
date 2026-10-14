#include "apu_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *recomp_lookup(unsigned long address){(void)address;abort();}
void *recomp_lookup_manual(unsigned long address){(void)address;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    MCPXAPUState *d=calloc(1,sizeof *d);CHECK(d);
    static float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    int16_t output[256][2],saved[256][2];
    d->regs[NV_PAPU_SECTL]=NV_PAPU_SECTL_XCNTMODE;
    for(unsigned frame=0;frame<8;frame++) {
        d->ep_frame_div=frame;
        for(unsigned i=0;i<NUM_SAMPLES_PER_FRAME;i++) {
            bins[0][i]=(float)(frame+1)/16;
            bins[1][i]=-(float)(frame+1)/32;
        }
        mcpx_apu_dsp_frame(d,bins);
    }
    memcpy(saved,d->monitor.frame_buf,sizeof saved);
    CHECK(mcpx_apu_monitor_render(d,output)==256);
    CHECK(memcmp(output,saved,sizeof output)==0);
    for(unsigned frame=0;frame<8;frame++)for(unsigned i=0;i<NUM_SAMPLES_PER_FRAME;i++) {
        CHECK(output[frame*NUM_SAMPLES_PER_FRAME+i][0]==(int16_t)((frame+1)/16.0f*32767));
        CHECK(output[frame*NUM_SAMPLES_PER_FRAME+i][1]==(int16_t)(-(float)(frame+1)/32.0f*32767));
    }
    CHECK(memcmp(d->monitor.frame_buf,saved,sizeof saved)==0);
    /* A halted pipeline must not repeat stale hardware PCM. */
    d->regs[NV_PAPU_FECTL]=NV_PAPU_FECTL_FEMETHMODE_HALTED;
    CHECK(mcpx_apu_monitor_render(d,output)==256);
    for(unsigned i=0;i<256;i++)CHECK(output[i][0]==0&&output[i][1]==0);
    CHECK(memcmp(d->monitor.frame_buf,saved,sizeof saved)==0);
    free(d);puts("Eight hardware audio subframes reach one 256-sample output frame intact");return 0;
}
