#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "xinput_xbox.h"
extern void xbox_FramebufferWindowStart(void);
extern int xbox_FramebufferKeyDown(int);
ptrdiff_t xbox_GetMemoryOffset(void){return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);if(window)PostMessageA(window,WM_CLOSE,0,0);return 1;}}while(0)
int main(void)
{
    HWND window=NULL;XBOX_INPUT_STATE state;
    _putenv_s("RECOMP_FB_WINDOW","1");_putenv_s("RECOMP_KEYBOARD","1");
    xbox_FramebufferWindowStart();
    /* Select only the window owned by this fixture. */
    for(unsigned attempt=0;attempt<200&&!window;attempt++) {
        HWND candidate=NULL;
        while((candidate=FindWindowExA(NULL,candidate,"XboxRecompFramebuffer",NULL))) {
            DWORD owner;GetWindowThreadProcessId(candidate,&owner);
            if(owner==GetCurrentProcessId()){window=candidate;break;}
        }
        if(!window)Sleep(10);
    }
    CHECK(window);
    CHECK(IsWindowVisible(window));
    SendMessageA(window,WM_KEYDOWN,'Z',0);
    SendMessageA(window,WM_KEYDOWN,VK_RETURN,0);
    SendMessageA(window,WM_KEYDOWN,VK_NUMPAD8,0);
    SendMessageA(window,WM_KEYDOWN,'3',0);
    CHECK(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    CHECK(state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    CHECK(state.Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]==255);
    CHECK(state.Gamepad.wButtons&XBOX_GAMEPAD_START);
    CHECK(state.Gamepad.sThumbLY==32767);
    SendMessageA(window,WM_KEYUP,'Z',0);CHECK(!xbox_FramebufferKeyDown('Z'));
    SendMessageA(window,WM_KILLFOCUS,0,0);
    CHECK(!xbox_FramebufferKeyDown(VK_RETURN));
    CHECK(!xbox_FramebufferKeyDown(VK_NUMPAD8));
    CHECK(!xbox_FramebufferKeyDown('3'));
    SendMessageA(window,WM_CLOSE,0,0);
    for(unsigned attempt=0;attempt<200&&IsWindow(window);attempt++)Sleep(10);
    CHECK(!IsWindow(window));
    puts("Native framebuffer keyboard accept/start/movement/fire and close passed");return 0;
}
