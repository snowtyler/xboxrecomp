param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [string]$Executable = 'brute_force_e3_perf152.exe',
    [ValidateRange(160,900)][int]$Seconds = 600,
    [switch]$Mute
)
$ErrorActionPreference = 'Stop'
# Normal graphics barriers isolate runtime/lifter changes from GET/coalescing.
$settingsTask = @{
    RECOMP_HW_RASTER='1'; RECOMP_HW_VERTEX='1'; RECOMP_NV2A_NOTIFY='1'
    RECOMP_FB_WINDOW='1'; RECOMP_KEYBOARD='1'; RECOMP_QUIET='1'
    RECOMP_AUDIO_TRACE='1'; RECOMP_AUDIO_MUTE=$(if ($Mute) {'1'} else {$null})
    RECOMP_HW_RELEASE_COALESCE=$null; RECOMP_HW_RELEASE_BARRIER=$null
    RECOMP_NV2A_IRQ_GATE=$null; RECOMP_NV2A_GET_PROGRESS=$null
    RECOMP_NV2A_GET_MONITOR=$null; RECOMP_NV2A_ORDER_TRACE=$null
    RECOMP_GPU_TIMING=$null; RECOMP_HW_GPU_TRACE=$null
    RECOMP_HW_COUNTERS=$null; RECOMP_HW_CPU_TIMING=$null
    RECOMP_HW_TEXTURE_HASH=$null; RECOMP_HW_TEXTURE_EPOCH=$null
    RECOMP_NV2A_PFB_POLL=$null; RECOMP_PERF_WINDOWS=$null
    RECOMP_AUDIO_BUF_SAMPLES='512'; RECOMP_AUDIO_BUF_COUNT='4'
    RECOMP_HW_PRESENT=$null; RECOMP_HW_TARGET_POOL=$null
    RECOMP_HW_TARGET_POOL_DISABLE=$null; RECOMP_HW_COLOR_RELOAD=$null
    RECOMP_HW_BULK_READBACK=$null; RECOMP_HW_READBACK_FENCE=$null
    RECOMP_HW_VSH_GATHER_ALL=$null
}
$originalTask = @{}
foreach ($keyTask in $settingsTask.Keys) {
    $originalTask[$keyTask] = [Environment]::GetEnvironmentVariable($keyTask, 'Process')
    if ($null -eq $settingsTask[$keyTask]) {
        Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue
    } else {
        Set-Item -LiteralPath "Env:$keyTask" -Value $settingsTask[$keyTask]
    }
    if ($null -eq $settingsTask[$keyTask] -and (Test-Path -LiteralPath "Env:$keyTask")) {
        throw "Disabled probe variable is still present: $keyTask"
    }
}
try {
    $routeTask = '75000:start:400,95000:start:400,105000:a:400,110000:down:400,115000:a:400,470000:y:1500,490000:y:2000,510000:lup:6000,535000:rt:4000,560000:rright:3000,590000:rt:3000'
    $capturesTask = '100:1,160:1'
    if ($Seconds -gt 470) {$capturesTask += ',470:1'}
    if ($Seconds -gt 550) {$capturesTask += ',550:1'}
    if ($Seconds -gt 590) {$capturesTask += ',590:1'}
    & (Join-Path $PSScriptRoot 'probe_runtime.ps1') -Name $Name -Executable $Executable `
        -Audio -DspDoorbell auto -Seconds $Seconds -Captures $capturesTask `
        -InspectFrom ($Seconds - 10) -PadScript $routeTask
    if ($LASTEXITCODE -ne 0) {throw 'Upstream integration probe runner failed.'}
} finally {
    foreach ($keyTask in $settingsTask.Keys) {
        if ($null -eq $originalTask[$keyTask]) {
            Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue
        } else {
            Set-Item -LiteralPath "Env:$keyTask" -Value $originalTask[$keyTask]
        }
    }
}
