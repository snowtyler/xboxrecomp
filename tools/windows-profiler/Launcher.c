#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

/* The scheduled action is native so CLR/profiler variables cannot inject code
 * before the managed broker has a chance to sanitize its child environment. */
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR args,int show) {
    const wchar_t expected[]=L"C:\\ProgramData\\BruteForceProfiler\\Launcher.exe";
    const wchar_t broker[]=L"C:\\ProgramData\\BruteForceProfiler\\Profiler.exe";
    wchar_t actual[32768];
    wchar_t command[]=L"\"C:\\ProgramData\\BruteForceProfiler\\Profiler.exe\" serve";
    wchar_t environment[]=
        L"APPDATA=C:\\ProgramData\\BruteForceProfiler\\profile\\Roaming\0"
        L"COMSPEC=C:\\Windows\\System32\\cmd.exe\0"
        L"LOCALAPPDATA=C:\\ProgramData\\BruteForceProfiler\\profile\\Local\0"
        L"PATH=C:\\Windows\\System32\0"
        L"ProgramData=C:\\ProgramData\0"
        L"ProgramFiles=C:\\Program Files\0"
        L"SystemDrive=C:\0"
        L"SystemRoot=C:\\Windows\0"
        L"TEMP=C:\\ProgramData\\BruteForceProfiler\\temp\0"
        L"TMP=C:\\ProgramData\\BruteForceProfiler\\temp\0"
        L"USERPROFILE=C:\\ProgramData\\BruteForceProfiler\\profile\0"
        L"WINDIR=C:\\Windows\0";
    STARTUPINFOW startup={sizeof startup};
    PROCESS_INFORMATION process={0};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    HANDLE job;
    DWORD code=1;
    (void)instance;(void)previous;(void)show;
    if(*args || !GetModuleFileNameW(NULL,actual,32768) || _wcsicmp(actual,expected))return 2;
    job=CreateJobObjectW(NULL,NULL);
    if(!job)return 3;
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits)){CloseHandle(job);return 4;}
    if(!CreateProcessW(broker,command,NULL,NULL,FALSE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,environment,L"C:\\ProgramData\\BruteForceProfiler",&startup,&process)){CloseHandle(job);return 5;}
    if(!AssignProcessToJobObject(job,process.hProcess) || ResumeThread(process.hThread)==(DWORD)-1){
        TerminateProcess(process.hProcess,6);code=6;
    } else {
        WaitForSingleObject(process.hProcess,INFINITE);
        GetExitCodeProcess(process.hProcess,&code);
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);
    return (int)code;
}
