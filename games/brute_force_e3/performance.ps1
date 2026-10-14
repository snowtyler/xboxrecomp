param(
    [Parameter(Mandatory=$true)][string]$Name,
    [string]$Executable = 'brute_force_e3_prototype.exe',
    [int]$Seconds = 850,
    [switch]$GpuPresent,
    [switch]$Replay,
    [switch]$PausedView,
    [switch]$PausedReplay,
    [switch]$DisableTargetPool,
    [switch]$TargetPool,
    [switch]$BulkReadback,
    [switch]$ReadbackFence,
    [switch]$GatherAll,
    [switch]$NormalGraphics,
    [switch]$ProductionBenchmark,
    [switch]$Counters,
    [switch]$CpuTiming,
    [switch]$ReleaseCoalesce,
    [switch]$ReleaseBarrier,
    [switch]$TextureHash,
    [switch]$TextureEpoch,
    [switch]$IrqGate,
    [switch]$OrderTrace,
    [switch]$TitleProbe
)
$ErrorActionPreference = 'Stop'
$env:RECOMP_HW_RASTER = '1'
$env:RECOMP_HW_VERTEX = '1'
$env:RECOMP_NV2A_NOTIFY = '1'
$env:RECOMP_FB_WINDOW = '1'
$env:RECOMP_KEYBOARD = '1'
$env:RECOMP_QUIET = '1'
$env:RECOMP_GPU_TIMING = if ($NormalGraphics -or $ProductionBenchmark -or $TitleProbe) { $null } else { '1' }
$env:RECOMP_HW_GPU_TRACE = if ($NormalGraphics -or $ProductionBenchmark -or $TitleProbe) { $null } else { '1' }
$env:RECOMP_HW_COUNTERS = if ($Counters) { '1' } else { $null }
$env:RECOMP_HW_CPU_TIMING = if ($CpuTiming) { '1' } else { $null }
$env:RECOMP_HW_RELEASE_COALESCE = if ($ReleaseCoalesce) { '1' } else { $null }
$env:RECOMP_HW_RELEASE_BARRIER = if ($ReleaseBarrier) { '1' } else { $null }
$env:RECOMP_HW_TEXTURE_HASH = if ($TextureHash) { '1' } else { $null }
$env:RECOMP_HW_TEXTURE_EPOCH = if ($TextureEpoch) { '1' } else { $null }
$env:RECOMP_NV2A_IRQ_GATE = if ($IrqGate) { '1' } else { $null }
$env:RECOMP_NV2A_ORDER_TRACE = if ($OrderTrace) { '1' } else { $null }
$env:RECOMP_EVENT_TRACE = if ($OrderTrace) { '0x003AAC9C' } else { $null }
$env:RECOMP_AUDIO_TRACE = '1'
$env:RECOMP_HW_PRESENT = if ($GpuPresent) { '1' } else { $null }
$env:RECOMP_AUDIO_MUTE = $null
$env:RECOMP_HW_COLOR_RELOAD = $null
$env:RECOMP_HW_TARGET_POOL_DISABLE = if ($DisableTargetPool) { '1' } else { $null }
$env:RECOMP_HW_TARGET_POOL = if ($TargetPool) { '1' } else { $null }
$env:RECOMP_HW_BULK_READBACK = if ($BulkReadback) { '1' } else { $null }
$env:RECOMP_HW_READBACK_FENCE = if ($ReadbackFence) { '1' } else { $null }
$env:RECOMP_HW_VSH_GATHER_ALL = if ($GatherAll) { '1' } else { $null }
$env:RECOMP_NV2A_PFB_POLL = $null
$env:RECOMP_PERF_WINDOWS = $null
$routeTask = '75000:start:400,95000:start:400,105000:a:400,110000:down:400,115000:a:400,470000:y:1500,490000:y:2000,510000:lup:6000,535000:rt:4000,560000:rright:3000,590000:rt:3000,620000:start:400,625000:a:400,665000:black:400,700000:lup:6000,735000:rt:3000,780000:rright:3000'
if ($ProductionBenchmark) {
    if ($NormalGraphics -or $Replay -or $PausedReplay) { throw 'ProductionBenchmark is a separate fixed-view timing route.' }
    $PausedView = $true
}
if ($NormalGraphics -and -not ($Replay -or $PausedReplay)) {
    throw 'NormalGraphics is a correctness route; use Replay or PausedReplay.'
}
if ($PausedReplay) {
    if ($Replay -or $Seconds -lt 1800) { throw 'PausedReplay needs at least1800 seconds and a separate route from Replay.' }
    $PausedView = $true
}
if ($PausedView) {
    if ($Replay) { throw 'Use separate replay and paused-view probes.' }
    $routeTask = '75000:start:400,95000:start:400,105000:a:400,110000:down:400,115000:a:400,470000:y:1500,490000:y:2000,510000:start:400'
    if (-not $NormalGraphics) { $env:RECOMP_PERF_WINDOWS = '560:590,600:630,650:680' }
}
if ($PausedReplay) {
    $routeTask += ',700000:a:400,715000:lup:6000,740000:rt:4000,765000:rright:3000,795000:rt:3000,870000:black:400,905000:lup:6000,940000:rt:3000,985000:rright:3000,1130000:x:600,1135000:a:600,1240000:y:1500,1440000:y:2000,1470000:lup:4000,1500000:rt:3000'
}
if ($Replay) {
    if ($Seconds -lt 1500) { throw 'Replay verification needs at least 1500 seconds.' }
    $routeTask += ',930000:x:600,935000:a:600,1040000:y:1500,1240000:y:2000,1270000:lup:4000,1300000:rt:3000'
}
$captureStartTask = if ($TitleProbe) { 125 } elseif ($PausedView) { 556 } else { 470 }
$captureDurationTask = if ($PausedView) { 3 } else { 4 }
$inspectStartTask = if ($TitleProbe) { 135 } elseif ($PausedReplay) { 1640 } elseif ($Replay) { 1360 } else { -1 }
$capturesTask = if ($PausedReplay) { '260:2,360:2,750:2,900:2,1080:2,1140:2,1450:2,1530:2,1660:2,1760:2' } else { '' }
$sampleStartTask = if ($NormalGraphics -or $ProductionBenchmark) { -1 } else { 540 }
if($TitleProbe) {
    if($Seconds -lt 160 -or $Seconds -gt 300){throw 'TitleProbe requires160–300 seconds.'}
    $capturesTask='100:2,160:2';$sampleStartTask=135
}
& (Join-Path $PSScriptRoot 'probe_runtime.ps1') -Name $Name -Executable $Executable `
    -Audio -DspDoorbell 0x83390810 -Seconds $Seconds -CaptureFrom $captureStartTask -CaptureFor $captureDurationTask -Captures $capturesTask `
    -SampleFrom $sampleStartTask -SampleFor 10 -InspectFrom $inspectStartTask -Measure:(-not $NormalGraphics) -PadScript $routeTask
