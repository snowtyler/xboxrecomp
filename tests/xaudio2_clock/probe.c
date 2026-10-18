/* Feed silent PCM to the real backend without a game. Run after gameplay,
 * never concurrently with a performance probe. No audio device is changed. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/apu/apu_xaudio2.h"
int main(int argc,char **argv)
{
    int seconds=argc>1?atoi(argv[1]):65;
    if(seconds<15||seconds>120)return 2;
    _putenv_s("RECOMP_AUDIO_BUF_SAMPLES","512");
    _putenv_s("RECOMP_AUDIO_BUF_COUNT","4");
    _putenv_s("RECOMP_AUDIO_DEVICE_TRACE","1");
    _putenv_s("RECOMP_AUDIO_MUTE","");
    if(!xa2_init())return 3;
    int16_t silence[256][2]={0};
    ULONGLONG end=GetTickCount64()+(ULONGLONG)seconds*1000;
    while(GetTickCount64()<end) {
        int queued=xa2_queued_samples();
        while(queued<2048) {
            if(!xa2_submit_samples(&silence[0][0],256)){xa2_shutdown();return 4;}
            queued+=256;
        }
        Sleep(1);
    }
    fprintf(stderr,"[CLOCK PROBE] seconds=%d played=%llu underruns=%u\n",
            seconds,(unsigned long long)xa2_samples_played(),xa2_underruns());
    xa2_shutdown();return 0;
}
