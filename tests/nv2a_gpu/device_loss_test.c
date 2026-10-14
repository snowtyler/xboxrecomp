#define COBJMACROS
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_hw_raster.h"
extern void nv2a_hw_test_device_loss(void);

static int remove_device(void)
{
    _putenv_s("RECOMP_HW_RASTER","1");
    static uint32_t pixels[32*32];
    Nv2aHwState state={0};Nv2aVshOutput vertices[3]={0};
    uint32_t indices[]={0,1,2};
    state.surface=1;state.width=state.height=state.clip_w=state.clip_h=32;
    state.pitch=128;state.bpp=4;state.color=(unsigned char*)pixels;
    state.depth_scale=1;state.color_mask=0x01010101;
    vertices[1].pos[0]=32;vertices[2].pos[1]=32;
    for(unsigned i=0;i<3;i++){vertices[i].pos[3]=1;vertices[i].d0[0]=vertices[i].d0[3]=1;}
    assert(nv2a_hw_draw(&state,vertices,3,indices,3));
    void *device,*context,*resource;
    assert(nv2a_hw_present_source(state.color,state.pitch,32,32,&device,&context,&resource));
    /* The fixture-only hook exercises the same fatal handler without removing
     * a real driver device or exposing an injection switch in game builds. */
    nv2a_hw_test_device_loss();
    return 1;
}
int main(int argc,char **argv)
{
    if(argc==2&&strcmp(argv[1],"--remove")==0)return remove_device();
    wchar_t executable[32768],command[32784];
    assert(GetModuleFileNameW(NULL,executable,32768));
    assert(swprintf_s(command,32784,L"\"%s\" --remove",executable)>0);
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};startup.cb=sizeof startup;
    assert(CreateProcessW(executable,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process));
    DWORD wait=WaitForSingleObject(process.hProcess,30000),code;
    if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
    assert(wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code));
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    assert(code==2);
    puts("Device removal exits explicitly with code 2");return 0;
}
