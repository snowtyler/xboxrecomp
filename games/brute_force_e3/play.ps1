param(
    [string]$GameDirectory = 'D:\Downloads\Brute Force E3 Prototype',
    [string]$Executable = 'brute_force_e3_prototype.exe',
    [switch]$SoftwareVertex,
    [switch]$TextureEpoch,
    [switch]$Audio = $true
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'analysis/runtime-probe.lock')) {
    throw 'A performance probe is active; wait for it to finish before launching another game.'
}
$binaryTask = Join-Path $PSScriptRoot "build\RelWithDebInfo\$Executable"
if (-not (Test-Path -LiteralPath $binaryTask -PathType Leaf)) {
    throw "Build executable not found: $binaryTask"
}
if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'default.xbe') -PathType Leaf)) {
    throw "Extracted prototype default.xbe not found in $GameDirectory"
}
$saveTask = Join-Path $PSScriptRoot 'saves'
$logTask = Join-Path $PSScriptRoot 'analysis'
New-Item -ItemType Directory -Path $saveTask, $logTask -Force | Out-Null
$stampTask = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$settingsTask = @{
    BRUTE_FORCE_E3_DIR = $GameDirectory
    BRUTE_FORCE_E3_SAVE_DIR = $saveTask
    BRUTE_FORCE_E3_FENCE_MIRROR = '1'
    RECOMP_PB_EXEC = '1'
    RECOMP_VBLANK = '1'
    RECOMP_NV2A_NOTIFY = '1'
    RECOMP_TITLE_KEVENTS = '1'
    RECOMP_USB = '1'
    RECOMP_USB_HC = '0'
    RECOMP_USB_NDP = '4'
    RECOMP_USB_PORT = '2'
    RECOMP_HW_RASTER = '1'
    RECOMP_HW_VERTEX = $(if ($SoftwareVertex) { $null } else { '1' })
    RECOMP_FB_WINDOW = '1'
    RECOMP_KEYBOARD = '1'
    RECOMP_QUIET = '1'
    RECOMP_AC97_READY = '1'
    RECOMP_APU_DSP_ACK = '0x83390810'
    RECOMP_AUDIO_MUTE = $(if ($Audio) { $null } else { '1' })
    RECOMP_WATCHDOG_SECS = $null
    RECOMP_PAD_SCRIPT = $null
    RECOMP_PAD_LIVE = $null
    RECOMP_FB_DUMP = $null
    RECOMP_FB_DUMP_FLIPS = $null
    RECOMP_FRAME_TRACE = $null
    RECOMP_SDK_WRITER_TRACE = $null
    RECOMP_SDK_JOURNAL_DUMP = $null
    RECOMP_PB_FAILURE_DUMP = $null
    RECOMP_PB_CALL_TRACE = $null
    RECOMP_GPU_TIMING = $null
    RECOMP_HW_GPU_TRACE = $null
    RECOMP_HW_COUNTERS = $null
    RECOMP_HW_CPU_TIMING = $null
    RECOMP_HW_RELEASE_COALESCE = $null
    RECOMP_HW_RELEASE_BARRIER = $null
    RECOMP_HW_TEXTURE_HASH = $null
    RECOMP_HW_TEXTURE_EPOCH = $(if ($TextureEpoch) { '1' } else { $null })
    RECOMP_HW_COLOR_RELOAD = $null
    RECOMP_HW_PRESENT = $null
    RECOMP_HW_TARGET_POOL = $null
    RECOMP_HW_TARGET_POOL_DISABLE = $null
    RECOMP_HW_BULK_READBACK = $null
    RECOMP_HW_READBACK_FENCE = $null
    RECOMP_HW_VSH_GATHER_ALL = $null
    RECOMP_NV2A_PFB_POLL = $null
    RECOMP_NV2A_IRQ_GATE = $null
    RECOMP_NV2A_ORDER_TRACE = $null
    RECOMP_EVENT_TRACE = $null
    RECOMP_PEEK = $null
    RECOMP_INPUT_TRACE = $null
    RECOMP_AUDIO_TRACE = $null
    RECOMP_AUDIO_DUMP = $null
    RECOMP_AUDIO_QUEUE_TRACE = $null
    RECOMP_STUB_TRACE = $null
}
$previousTask = @{}
$processTask = $null
try {
    foreach ($keyTask in $settingsTask.Keys) {
        $previousTask[$keyTask] = [Environment]::GetEnvironmentVariable($keyTask, 'Process')
        # PowerShell's string binding converts $null in SetEnvironmentVariable
        # to an empty string. Native getenv checks presence, so remove disabled
        # flags through the environment provider instead.
        if ($null -eq $settingsTask[$keyTask]) {
            Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($keyTask, $settingsTask[$keyTask], 'Process')
        }
    }
    # The child creates its visible game window; hide its diagnostic console.
    $processTask = Start-Process -FilePath $binaryTask -WorkingDirectory $PSScriptRoot `
        -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logTask "play-$stampTask.stdout.log") `
        -RedirectStandardError (Join-Path $logTask "play-$stampTask.stderr.log")
} finally {
    foreach ($keyTask in $previousTask.Keys) {
        if ($null -eq $previousTask[$keyTask]) {
            Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($keyTask, $previousTask[$keyTask], 'Process')
        }
    }
}
Write-Host 'Controls: Enter=Start, Z=A/accept, S=Y/skip tutorial menus, arrows=menus.'
Write-Host 'NumPad 8/2/4/6=move, I/K/J/L=look, 3=fire. An XInput controller also works.'
Write-Host 'Close the game window or press Ctrl+C here to exit.'
$hadWindowTask = $false
$closedTask = $false
try {
    while (-not $processTask.HasExited) {
        $processTask.Refresh()
        if ($processTask.MainWindowHandle -ne [IntPtr]::Zero) {
            $hadWindowTask = $true
        } elseif ($hadWindowTask) {
            # Closing the framebuffer window stops its display thread. End
            # this launcher's child as well so the Xbox loop does not linger.
            $closedTask = $true
            $processTask.Kill()
            break
        }
        Start-Sleep -Milliseconds 250
    }
    $processTask.WaitForExit()
    if (-not $closedTask -and $processTask.ExitCode -ne 0) {
        throw "Game exited with code $($processTask.ExitCode). Logs: $logTask\play-$stampTask.*.log"
    }
} finally {
    if (-not $processTask.HasExited) {
        $processTask.Kill()
        $processTask.WaitForExit()
    }
    $processTask.Dispose()
}
