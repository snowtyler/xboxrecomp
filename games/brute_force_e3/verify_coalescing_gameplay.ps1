param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ShortPairs,
    [string]$Executable='brute_force_e3_perf159.exe',
    [ValidateRange(550,900)][int]$Seconds=640,
    [ValidateRange(640,900)][int]$LeanSeconds=640
)
$ErrorActionPreference='Stop'
$analysisTask=Join-Path $PSScriptRoot 'analysis'
$rootTask=Split-Path (Split-Path $PSScriptRoot)
$statePathTask=Join-Path $analysisTask "$Name-gameplay-workflow.json"
if(Test-Path -LiteralPath $statePathTask){throw 'Workflow name already exists.'}
$pairPathTask=Join-Path $analysisTask "$ShortPairs-get-verification.json"
$stateTask=@{status='waiting_for_short_pairs';current=$null;completed=@();failures=@();executable=$Executable;short_pairs=$ShortPairs}
function Save-GameplayStateTask {$stateTask|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $statePathTask}
trap {$stateTask.status='failed';$stateTask.error=$_.Exception.Message;Save-GameplayStateTask;throw}
Save-GameplayStateTask
# This is one bounded, sequential local test workflow, not a scheduled job.
$deadlineTask=(Get-Date).AddMinutes(45)
while($true) {
    if(Test-Path (Join-Path $analysisTask "$Name-stop.flag")){throw 'Workflow cancelled.'}
    if((Get-Date)-gt $deadlineTask){throw 'Short-pair prerequisite timed out.'}
    if(Test-Path $pairPathTask) {
        try {$pairsTask=Get-Content -LiteralPath $pairPathTask -Raw|ConvertFrom-Json}
        catch {Start-Sleep -Seconds 1;continue} # Writer may be replacing the JSON.
        if($pairsTask.status -eq 'failed'){throw 'Short-pair prerequisite failed; inspect its evidence.'}
        if($pairsTask.status -eq 'awaiting_capture_review') {
            if($pairsTask.pairs -lt 5 -or $pairsTask.barrier -or $pairsTask.completed.Count -ne $pairsTask.pairs*2) {
                throw 'Expected five complete coalesced short pairs.'
            }
            break
        }
    }
    Start-Sleep -Seconds 5
}
if(Test-Path (Join-Path $analysisTask 'runtime-probe.lock')){throw 'Another probe is active.'}
$stateTask.status='running';$stateTask.current='native_gpu_checks';Save-GameplayStateTask
& ctest --test-dir (Join-Path $rootTask 'build/nv2a-semaphore') -C Release --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Semaphore regression failed.'}
& ctest --test-dir (Join-Path $rootTask 'build/nv2a-gpu') -C Release -R 'raster-coherence|coalesced-inputs' --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Raster/input regression failed.'}
$flipScriptTask='f4500:start:24,f5700:start:24,f6300:a:24,f6600:down:24,f6900:a:24,f28000:y:24,f28500:y:24,f29000:lup:12,f29500:rt:24,f30000:rright:4,f30500:rt:24'
$captureFlipsTask='28000,28500,29000,29500,30000,30500,31000'
foreach($armTask in @('barrier','coalesced','lean_barrier')) {
    if(Test-Path (Join-Path $analysisTask "$Name-stop.flag")){throw 'Workflow cancelled.'}
    $probeTask="${Name}_${armTask}"
    $stateTask.current=$probeTask;Save-GameplayStateTask
    try {
        if($armTask -eq 'lean_barrier') {
            & (Join-Path $PSScriptRoot 'verify_upstream_v014.ps1') -Name $probeTask -Executable $Executable `
                -Seconds $LeanSeconds -CombatProfile -LeanProfile -AudioTiming -UseDefaultGetProgress
        } else {
            & (Join-Path $PSScriptRoot 'verify_upstream_v014.ps1') -Name $probeTask -Executable $Executable `
                -Seconds $Seconds -FlipScript $flipScriptTask -CaptureFlips $captureFlipsTask `
                -ReleaseCoalesce:($armTask -eq 'coalesced') -UseDefaultGetProgress -SyncMetrics -AudioTiming
        }
        $stateTask.completed+=$probeTask
    } catch {
        # The lean baseline is independent of a failed experimental arm.
        $stateTask.failures+=@{probe=$probeTask;error=$_.Exception.Message}
    }
    Save-GameplayStateTask
}
$stateTask.current=$null
$stateTask.status=if($stateTask.failures.Count){'completed_with_failures'}else{'awaiting_scene_review'}
Save-GameplayStateTask
