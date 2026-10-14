/* Device save roots must agree with T:/U:, including component boundaries. */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

void *recomp_lookup(ULONG a) { (void)a; abort(); }
void *recomp_lookup_manual(ULONG a) { (void)a; abort(); }

int main(void)
{
    char temp[MAX_PATH], save[MAX_PATH], name[MAX_PATH];
    wchar_t got[MAX_PATH], expected[MAX_PATH], drive[MAX_PATH];
    int ok = 1;
    GetTempPathA(MAX_PATH, temp);
    snprintf(save, sizeof(save), "%ssave-path-test-%lu", temp, GetCurrentProcessId());
    if (!CreateDirectoryA(save, NULL)) return 2;
    xbox_path_init("C:\\unused-game", save);

    if (!xbox_translate_path("T:\\", drive, MAX_PATH)
        || !xbox_translate_path("\\Device\\Harddisk0\\Partition1\\TDATA", got, MAX_PATH)
        || wcscmp(got, drive)) ok = 0;
    if (!xbox_translate_path("U:\\slot\\save.dat", drive, MAX_PATH)
        || !xbox_translate_path("\\Device\\Harddisk0\\partition1\\udata\\slot\\save.dat", got, MAX_PATH)
        || wcscmp(got, drive)) ok = 0;
    if (!xbox_translate_path("T:\\4D53001E", drive, MAX_PATH)
        || !xbox_translate_path("\\Device\\Harddisk0\\Partition1\\TDATA\\4D53001E", got, MAX_PATH)
        || wcscmp(got, drive)) ok = 0;
    /* A similarly named asset directory keeps the general Partition1 rule. */
    swprintf(expected, MAX_PATH, L"C:\\unused-game\\TDATAX\\asset.bin");
    if (!xbox_translate_path("\\Device\\Harddisk0\\Partition1\\TDATAX\\asset.bin", got, MAX_PATH)
        || wcscmp(got, expected)) ok = 0;
    MultiByteToWideChar(CP_UTF8, 0, save, -1, expected, MAX_PATH);
    wcscat_s(expected, MAX_PATH, L"\\Partition1.img");
    if (!xbox_translate_path("\\Device\\Harddisk0\\Partition1", got, MAX_PATH)
        || wcscmp(got, expected)) ok = 0;

    for (int i = 0; i <= 5; i++) {
        snprintf(name, sizeof(name), "%s\\Partition%d.img", save, i);
        DeleteFileA(name);
    }
    const char *dirs[] = {"TitleData", "UserData", "Cache", "SystemData"};
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "%s\\%s", save, dirs[i]);
        RemoveDirectoryA(name);
    }
    RemoveDirectoryA(save);
    puts(ok ? "PASS: device save roots, descendants, and component boundaries" : "FAIL: save path mapping");
    return ok ? 0 : 1;
}
