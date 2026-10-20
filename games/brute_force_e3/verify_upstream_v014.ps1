param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [string]$Executable = 'brute_force_e3_prototype.exe',
    [ValidateRange(160,1800)][int]$Seconds = 600,
    [switch]$Mute,
    [switch]$GetProgress = $true,
    [switch]$UseDefaultGetProgress,
    [switch]$GetMonitor = $true,
    [switch]$ReleaseCoalesce,
    [switch]$Replay,
    [switch]$AudioEvidence,
    [switch]$AudioTiming,
    [switch]$GuestStateEvidence,
    [switch]$CombatProfile,
    [switch]$LeanProfile,
    [switch]$SyncMetrics,
    [string]$FlipScript,
    [string]$CaptureFlips
)
$ErrorActionPreference = 'Stop'
if($LeanProfile -and (-not $CombatProfile -or $AudioEvidence)) {
    throw 'LeanProfile requires CombatProfile, with AudioTiming instead of WAV evidence.'
}
if ($CombatProfile -and ($Seconds -lt 640 -or $Replay -or $FlipScript)) {
    throw 'CombatProfile needs at least 640 seconds and its own combat route.'
}
if ($UseDefaultGetProgress) {
    if (-not $GetMonitor) {throw 'Default-policy qualification requires the GET monitor.'}
    $GetProgress=$true
}
if ([bool]$FlipScript -xor [bool]$CaptureFlips) {throw 'Flip comparison requires both FlipScript and CaptureFlips.'}
if ($FlipScript) {
    if ($Replay) {throw 'Flip comparison and replay qualification use separate routes.'}
    if ($FlipScript -notmatch '^f\d+:[a-zA-Z0-9+\-]+:\d+(,f\d+:[a-zA-Z0-9+\-]+:\d+)*$') {
        throw 'FlipScript requires f<flip>:<buttons>:<hold flips> entries.'
    }
    if ($CaptureFlips -notmatch '^\d+(-\d+)?(,\d+(-\d+)?)*$') {throw 'CaptureFlips requires N or N-M ranges.'}
    & python (Join-Path $PSScriptRoot 'validate_flip_route.py') --validate-only --script $FlipScript --captures $CaptureFlips
    if ($LASTEXITCODE -ne 0) {throw 'Invalid flip route or capture ranges.'}
}
if ($Replay -and $Seconds -lt 1500) { throw 'Replay requires at least 1500 seconds.' }
if (($Replay -or $FlipScript) -and -not ($GetProgress -and $GetMonitor)) {
    throw 'Replay and flip comparisons require monitored GET progress.'
}
if ($GetProgress -and -not $GetMonitor) { throw 'GET qualification requires the monitor.' }
if ($ReleaseCoalesce -and -not ($GetProgress -and $GetMonitor)) {
    throw 'Coalesced gameplay comparison requires monitored GET progress.'
}
if (($AudioEvidence -or $AudioTiming -or $SyncMetrics) -and -not ($GetProgress -and $GetMonitor)) {
    throw 'Audio evidence requires monitored GET progress as its graphics baseline.'
}
$settingsTask = @{
    RECOMP_HW_RASTER='1'; RECOMP_HW_VERTEX='1'; RECOMP_NV2A_NOTIFY='1'
    RECOMP_FB_WINDOW='1'; RECOMP_KEYBOARD='1'; RECOMP_QUIET='1'
    RECOMP_AUDIO_TRACE='1'; RECOMP_AUDIO_MUTE=$(if ($Mute) {'1'} else {$null})
    RECOMP_HW_RELEASE_COALESCE=$(if ($ReleaseCoalesce) {'1'} else {$null}); RECOMP_HW_RELEASE_BARRIER=$null
    RECOMP_NV2A_IRQ_GATE=$null; RECOMP_NV2A_GET_PROGRESS=$(if ($UseDefaultGetProgress) {$null} elseif ($GetProgress) {'1'} else {'0'})
    RECOMP_NV2A_GET_MONITOR=$(if ($GetMonitor) {'1'} else {$null}); RECOMP_NV2A_ORDER_TRACE=$null
    RECOMP_AUDIO_HOST=$(if ($AudioEvidence -or $AudioTiming) {'1'} else {$null})
    RECOMP_AUDIO_DEVICE_TRACE=$(if ($AudioEvidence -or $AudioTiming) {'1'} else {$null})
    RECOMP_AUDIO_CPU_TRACE=$(if ($AudioEvidence -or $AudioTiming) {'1'} else {$null})
    RECOMP_AUDIO_WAV=$(if ($AudioEvidence) {Join-Path $PSScriptRoot "analysis/$Name.wav"} else {$null})
    RECOMP_AUDIO_WAV_SECS=$(if ($AudioEvidence) {[string]$Seconds} else {$null})
    RECOMP_APU_TRACE=$null
    RECOMP_BRUTE_FORCE_ROUTE_STATE=$(if ($Replay -or $GuestStateEvidence) {'1'} else {$null})
    RECOMP_BRUTE_FORCE_WAIT_PROFILE=$(if ($CombatProfile -or $SyncMetrics) {'1'} else {$null})
    RECOMP_COMBAT_PROFILE_LEAN=$(if ($LeanProfile) {'1'} else {$null})
    RECOMP_PAD_SCRIPT_EXCLUSIVE=$(if ($FlipScript -or $CombatProfile) {'1'} else {$null})
    RECOMP_FB_DUMP_GUEST_FLIPS=$(if ($CaptureFlips) {$CaptureFlips} else {$null})
    RECOMP_GPU_TIMING=$null; RECOMP_HW_GPU_TRACE=$null
    RECOMP_HW_COUNTERS=$(if ($CombatProfile -or $SyncMetrics) {'1'} else {$null})
    RECOMP_HW_CPU_TIMING=$(if ($CombatProfile -or $SyncMetrics) {'1'} else {$null})
    RECOMP_HW_SYNC_CPU=$(if (($CombatProfile -or $SyncMetrics) -and -not $LeanProfile) {'1'} else {$null})
    RECOMP_HW_TEXTURE_HASH=$null; RECOMP_HW_TEXTURE_EPOCH=$null
    RECOMP_NV2A_PFB_POLL=$null
    RECOMP_PERF_WINDOWS=$(if ($CombatProfile) {'500:530,565:585,600:615,620:635'} else {$null})
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
$recordersTask=@()
try {
    if($CombatProfile -or $SyncMetrics) {
        $shellTask=(Get-Process -Id $PID).Path
        foreach($recorderTask in @('monitor_gpu.ps1','monitor_nvidia.ps1')) {
            $recordersTask += Start-Process -FilePath $shellTask -WindowStyle Hidden -PassThru `
                -ArgumentList @('-NoProfile','-File',(Join-Path $PSScriptRoot $recorderTask),'-Name',$Name,'-Seconds',($Seconds+30)) `
                -RedirectStandardOutput (Join-Path $PSScriptRoot "analysis/$Name-$recorderTask.stdout.log") `
                -RedirectStandardError (Join-Path $PSScriptRoot "analysis/$Name-$recorderTask.stderr.log")
        }
    }
    $routeTask = '75000:start:400,95000:start:400,105000:a:400,110000:down:400,115000:a:400,470000:y:1500,490000:y:2000,510000:lup:6000,535000:rt:4000,560000:rright:3000,590000:rt:3000'
    if ($Replay) {
        $routeTask += ',620000:start:400,625000:a:400,665000:black:400,700000:lup:6000,735000:rt:3000,780000:rright:3000,930000:x:600,935000:a:600,1040000:y:1500,1240000:y:2000,1270000:lup:4000,1300000:rt:3000'
    }
    if ($CombatProfile) {
        # The first profile's three-second turn ended in close rock views.
        # Shorter future controls still require capture review before comparison.
        $routeTask=$routeTask.Replace('560000:rright:3000','560000:rright:250')
        $routeTask += ',605000:lup:2000,620000:rt:4000'
    }
    $capturesTask = '100:1,160:1'
    if ($Seconds -gt 470) {$capturesTask += ',470:1'}
    if ($Seconds -gt 550) {$capturesTask += ',550:1'}
    if ($Seconds -gt 590) {$capturesTask += ',590:1'}
    if ($CombatProfile) {$capturesTask += ',610:1,630:1'}
    if ($Replay) {$capturesTask += ',750:1,900:1,940:1,1050:1,1250:1,1320:1,1450:1'}
    if ($FlipScript) {$routeTask=$FlipScript;$capturesTask=''}
    if ($LeanProfile) {$capturesTask=''}
    & (Join-Path $PSScriptRoot 'probe_runtime.ps1') -Name $Name -Executable $Executable `
        -Audio -DspDoorbell auto -Seconds $Seconds -Captures $capturesTask `
        -InspectFrom $(if($LeanProfile){$Seconds - 5}else{$Seconds - 10}) -StateInterval $(if ($Replay -or $GuestStateEvidence) {0.25} else {0}) -PadScript $routeTask `
        -Measure:($CombatProfile -or $SyncMetrics) -SampleFrom $(if ($CombatProfile -and -not $LeanProfile) {540} else {-1}) -SampleFor 20 -NoSurfaceDumps:$LeanProfile
    if ($LASTEXITCODE -ne 0) {throw 'Upstream integration probe runner failed.'}
    if ($GetMonitor) {
        & python (Join-Path $PSScriptRoot 'analyze_get_progress.py') (Join-Path $PSScriptRoot "analysis/$Name") `
            --monitor-only --output (Join-Path $PSScriptRoot "analysis/$Name-get-monitor.json")
        if ($LASTEXITCODE -ne 0) {throw 'GET monitor qualification failed.'}
        $summaryTask=Get-Content -LiteralPath (Join-Path $PSScriptRoot "analysis/$Name-get-monitor.json") -Raw | ConvertFrom-Json
        if ($GetProgress -and -not $summaryTask.progress) {throw 'The native policy did not enable requested GET progress.'}
    }
    if ($Replay) {
        & python (Join-Path $PSScriptRoot 'validate_guest_route.py') (Join-Path $PSScriptRoot "analysis/$Name") `
            --output (Join-Path $PSScriptRoot "analysis/$Name-guest-route.json")
        if ($LASTEXITCODE -ne 0) {throw 'Guest death/replay transitions were not proven.'}
    }
    if ($CombatProfile -or $SyncMetrics) {
        $flipMetricArgsTask=if($FlipScript){@('--flip-windows','28500:29000,29000:29500,29500:30000,30000:30500,30500:31000')}else{@()}
        & python (Join-Path $PSScriptRoot 'analyze_combat_profile.py') (Join-Path $PSScriptRoot "analysis/$Name") `
            --output (Join-Path $PSScriptRoot "analysis/$Name-combat.json") @flipMetricArgsTask
        if ($LASTEXITCODE -ne 0) {throw 'Combat profile observations are incomplete.'}
    }
    if ($FlipScript) {
        & python (Join-Path $PSScriptRoot 'validate_flip_route.py') (Join-Path $PSScriptRoot "analysis/$Name") `
            --output (Join-Path $PSScriptRoot "analysis/$Name-flip-route.json")
        if ($LASTEXITCODE -ne 0) {throw 'Flip input/capture receipts failed.'}
    }
} finally {
    foreach($recorderProcessTask in $recordersTask) {
        if(-not $recorderProcessTask.HasExited -and -not $recorderProcessTask.WaitForExit(8000)) {
            $recorderProcessTask.Kill();$recorderProcessTask.WaitForExit()
        }
        $recorderProcessTask.Dispose()
    }
    foreach ($keyTask in $settingsTask.Keys) {
        if ($null -eq $originalTask[$keyTask]) {
            Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue
        } else {
            Set-Item -LiteralPath "Env:$keyTask" -Value $originalTask[$keyTask]
        }
    }
}
