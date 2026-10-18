param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [string]$Executable='brute_force_e3_perf151.exe',
    [ValidateRange(1,10)][int]$Pairs=5,
    [ValidateRange(160,300)][int]$Seconds=180,
    [switch]$Barrier
)
$ErrorActionPreference='Stop'
$analysisTask=Join-Path $PSScriptRoot 'analysis'
$statePathTask=Join-Path $analysisTask "$Name-get-verification.json"
if(Test-Path -LiteralPath $statePathTask){throw 'Workflow name already exists.'}
$stateTask=@{status='running';current=$null;completed=@();executable=$Executable;pairs=$Pairs;barrier=[bool]$Barrier}
function Save-GetStateTask {$stateTask|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $statePathTask}
try {
    Save-GetStateTask
    for($pairTask=1;$pairTask -le $Pairs;$pairTask++) {
        foreach($progressTask in @($false,$true)) {
            if(Test-Path -LiteralPath (Join-Path $analysisTask "$Name-stop.flag")){throw 'Workflow cancelled.'}
            if(Test-Path -LiteralPath (Join-Path $analysisTask 'runtime-probe.lock')){throw 'Another probe is active.'}
            if(@(Get-Process -Name 'brute_force_e3*' -ErrorAction SilentlyContinue).Count){throw 'A Brute Force process is already running.'}
            $armTask=if($progressTask){'progress'}else{'control'}
            $probeTask="${Name}_${armTask}_$pairTask"
            $stateTask.current=$probeTask;Save-GetStateTask
            & (Join-Path $PSScriptRoot 'performance.ps1') -Name $probeTask -Executable $Executable -Seconds $Seconds `
                -Counters -OrderTrace -TitleProbe -GetMonitor -GetProgress:$progressTask -ReleaseCoalesce:(-not $Barrier)
            if($LASTEXITCODE -ne 0){throw 'Probe runner failed.'}
            & python (Join-Path $PSScriptRoot 'analyze_get_progress.py') (Join-Path $analysisTask $probeTask) `
                --output (Join-Path $analysisTask "$probeTask-get.json")
            if($LASTEXITCODE -ne 0){throw "GET checks failed for $probeTask; inspect its summary."}
            $stateTask.completed+=$probeTask;Save-GetStateTask
        }
    }
    $stateTask.status='awaiting_capture_review';$stateTask.current=$null;Save-GetStateTask
} catch {$stateTask.status='failed';$stateTask.error=$_.Exception.Message;Save-GetStateTask;throw}
