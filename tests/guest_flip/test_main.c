/* Actual controller parser/report path; the host pad and wall clock are fake. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
static DWORD milliseconds=1000;
#define GetTickCount() milliseconds
#include "../../src/usb/usb_gamepad.c"
#include "../../src/kernel/nv2a_flip_capture.h"
#include "../../src/kernel/nv2a_get_config.h"
#include "../../games/brute_force_e3/src/guest_probe_memory.h"
static int host_reads;
DWORD xbox_InputGetState(DWORD pad,XBOX_INPUT_STATE *out)
{(void)pad;host_reads++;memset(out,0,sizeof(*out));out->Gamepad.wButtons=0x10;return 0;}
int xbox_FramebufferKeyDown(int vk){(void)vk;return 0;}

int main(void)
{
    uint8_t report[20];
    _putenv_s("RECOMP_PAD_SCRIPT","f0010:a:03,f0012:p2-rt:02,40:b:15");
    _putenv_s("RECOMP_PAD_SCRIPT_EXCLUSIVE","1");
    _putenv_s("RECOMP_PAD_LIVE","");_putenv_s("RECOMP_PAD_PRESS","");
    _putenv_s("RECOMP_INPUT_DIAG","");
    usb_gamepad_set_guest_flip(0);
    assert(usb_gamepad_report(0,report,20)==20&&!report[2]&&!report[4]);
    milliseconds=1045;usb_gamepad_report(0,report,20);assert(report[5]==255);
    milliseconds=1060;usb_gamepad_report(0,report,20);assert(report[5]==0);
    milliseconds=500000;usb_gamepad_set_guest_flip(9);
    usb_gamepad_report(0,report,20);assert(!report[4]);
    usb_gamepad_set_guest_flip(10);usb_gamepad_report(0,report,20);assert(report[4]==255);
    milliseconds=1000000;usb_gamepad_report(0,report,20);assert(report[4]==255);
    usb_gamepad_set_guest_flip(12);usb_gamepad_report(0,report,20);assert(report[4]==255);
    usb_gamepad_report(1,report,20);assert(report[11]==255&&!report[4]);
    usb_gamepad_set_guest_flip(13);usb_gamepad_report(0,report,20);assert(!report[4]);
    usb_gamepad_report(1,report,20);assert(report[11]==255);
    usb_gamepad_set_guest_flip(14);usb_gamepad_report(1,report,20);assert(!report[11]);
    assert(host_reads==0); /* No keyboard/controller perturbation in this mode. */
    _putenv_s("RECOMP_PAD_SCRIPT_EXCLUSIVE","0");
    usb_gamepad_report(0,report,20);assert(host_reads==1&&report[2]==0x10);

    assert(nv2a_flip_capture_contains("10,20-22,30",10)==1);
    assert(nv2a_flip_capture_contains("10,20-22,30",21)==1);
    assert(nv2a_flip_capture_contains("10,20-22,30",19)==0);
    assert(nv2a_flip_capture_contains("10,9-8",10)==-1);
    assert(nv2a_flip_capture_contains("10,",10)==-1);
    assert(nv2a_flip_capture_contains("10x",10)==-1);
    assert(nv2a_flip_capture_contains("4294967296",0)==-1);
    assert(nv2a_flip_capture_contains("4294967295",UINT32_MAX)==1);
    assert(nv2a_flip_capture_contains("",10)==0);
    assert(brute_force_probe_readable(0x1000000,20));
    assert(brute_force_probe_readable(0x80E00000,0xC38));
    assert(!brute_force_probe_readable(0,4));
    assert(!brute_force_probe_readable(0x83FFFFF0,20));
    assert(!brute_force_probe_readable(0xFFFFFFFF,4));

    _putenv_s("RECOMP_NV2A_GET_PROGRESS","0");
    assert(nv2a_get_flag("RECOMP_NV2A_GET_PROGRESS",1)==0); /* future default on */
    _putenv_s("RECOMP_NV2A_GET_PROGRESS","1");assert(nv2a_get_progress_enabled()==1);
    _putenv_s("RECOMP_NV2A_GET_PROGRESS","");assert(nv2a_get_progress_enabled()==1);
    _putenv_s("RECOMP_NV2A_GET_MONITOR","0");assert(nv2a_get_monitor_enabled()==0);
    _putenv_s("RECOMP_NV2A_GET_MONITOR","1");assert(nv2a_get_monitor_enabled()==1);
    _putenv_s("RECOMP_NV2A_GET_MONITOR","");assert(nv2a_get_monitor_enabled()==0);
    puts("Flip input timing, legacy milliseconds, exclusive input, capture ranges and parsed GET flags passed.");
    return 0;
}
