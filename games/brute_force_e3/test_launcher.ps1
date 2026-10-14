param(
    [ValidatePattern('^(brute_force_e3_perf[0-9]+|brute_force_e3_prototype)\.exe$')]
    [string]$Executable = 'brute_force_e3_perf132.exe',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name = 'test-launcher132-final',
    [int]$Seconds = 90,
    [switch]$TextureEpoch
)
$ErrorActionPreference = 'Stop'
$binaryTask = Join-Path $PSScriptRoot "build/RelWithDebInfo/$Executable"
$binaryTask = (Get-Item -LiteralPath $binaryTask).FullName
$analysisTask = Join-Path $PSScriptRoot 'analysis'
$shellTask = (Get-Process -Id $PID).Path
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OwnedLaunchWindow {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
}
'@
$launcherTask = $null
$gameTask = $null
$windowTask = [IntPtr]::Zero
$resultTask = $null
try {
    $argumentsTask = @('-NoProfile','-File', (Join-Path $PSScriptRoot 'play.ps1'),'-Executable',$Executable)
    if ($TextureEpoch) { $argumentsTask += '-TextureEpoch' }
    $launcherTask = Start-Process -FilePath $shellTask -ArgumentList $argumentsTask -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $analysisTask "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $analysisTask "$Name.stderr.log")
    $deadlineTask = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadlineTask) {
        if ($launcherTask.HasExited) { throw 'Launcher exited before an owned visible game window appeared.' }
        if (-not $gameTask) {
            $childTask = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($launcherTask.Id)" |
                Where-Object { $_.Name -eq $Executable -and $_.ExecutablePath -eq $binaryTask } |
                Select-Object -First 1
            if ($childTask) { $gameTask = Get-Process -Id $childTask.ProcessId }
        }
        if ($gameTask) {
            $gameTask.Refresh()
            if ($gameTask.HasExited) { throw 'Owned game exited before creating its window.' }
            $windowTask = $gameTask.MainWindowHandle
            [uint32]$ownerTask = 0
            if ($windowTask -ne [IntPtr]::Zero) {
                [void][OwnedLaunchWindow]::GetWindowThreadProcessId($windowTask,[ref]$ownerTask)
                if ($ownerTask -eq $gameTask.Id -and [OwnedLaunchWindow]::IsWindowVisible($windowTask)) { break }
            }
        }
        Start-Sleep -Milliseconds 200
    }
    if ($windowTask -eq [IntPtr]::Zero -or -not [OwnedLaunchWindow]::IsWindowVisible($windowTask)) {
        throw 'Timed out waiting for the owned visible framebuffer window.'
    }
    # Let the launcher's own poll observe the window before requesting close.
    Start-Sleep -Seconds 2
    [uint32]$ownerTask = 0
    [void][OwnedLaunchWindow]::GetWindowThreadProcessId($windowTask,[ref]$ownerTask)
    if ($ownerTask -ne $gameTask.Id) { throw 'Window ownership changed; refusing to close it.' }
    if (-not [OwnedLaunchWindow]::PostMessage($windowTask,0x10,[IntPtr]::Zero,[IntPtr]::Zero)) {
        throw 'Owned WM_CLOSE failed.'
    }
    if (-not $launcherTask.WaitForExit(10000)) { throw 'Launcher did not exit after owned window closure.' }
    if (-not $gameTask.WaitForExit(5000)) { throw 'Owned child remained alive after window closure.' }
    if ($launcherTask.ExitCode -ne 0) { throw "Launcher failed: $($launcherTask.ExitCode)" }
    $resultTask = [ordered]@{
        executable = $binaryTask
        sha256 = (Get-FileHash -LiteralPath $binaryTask -Algorithm SHA256).Hash.ToLowerInvariant()
        texture_epoch = [bool]$TextureEpoch
        launcher_pid = $launcherTask.Id
        game_pid = $gameTask.Id
        window_seen = $true
        visible = $true
        closed = $true
        child_exited = $true
        launcher_exit_code = $launcherTask.ExitCode
    }
    $resultTask | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $analysisTask "$Name.json")
    $resultTask | ConvertTo-Json
} finally {
    # Only processes created and verified as this test's descendants are owned.
    if ($gameTask -and -not $gameTask.HasExited) { $gameTask.Kill(); $gameTask.WaitForExit() }
    if ($launcherTask -and -not $launcherTask.HasExited) { $launcherTask.Kill(); $launcherTask.WaitForExit() }
    if ($gameTask) { $gameTask.Dispose() }
    if ($launcherTask) { $launcherTask.Dispose() }
}
