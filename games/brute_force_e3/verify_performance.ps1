param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [Parameter(Mandatory=$true)][string]$Executable,
    [ValidateSet('Overhead','Release','TextureHash','TextureEpoch','TextureCombined','Combined')][string]$Experiment = 'Release',
    [string]$BaselineExecutable = 'brute_force_e3_perf132.exe',
    [ValidateRange(3,10)][int]$Pairs = 3,
    [switch]$ReplayGates,
    [switch]$ConcurrentWorkload,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ResumeComparison
)
$ErrorActionPreference = 'Stop'
$recorderShellTask = (Get-Process -Id $PID).Path
$analysisTask = Join-Path $PSScriptRoot 'analysis'
New-Item -ItemType Directory -Force -Path $analysisTask | Out-Null
$stateTask = Join-Path $analysisTask "$Name-verification.json"
if (Test-Path -LiteralPath $stateTask) { throw 'Verification name already exists.' }
$stateDataTask = [ordered]@{ status='running'; experiment=$Experiment; executable=$Executable; completed=@(); current=$null; resume_comparison=$ResumeComparison; concurrent_workload=[bool]$ConcurrentWorkload }
function Save-StateTask {
    $stateDataTask | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stateTask
}
function Invoke-ProbeTask([string]$ProbeName, [bool]$Enabled, [bool]$Replay) {
    if (Test-Path -LiteralPath (Join-Path $analysisTask 'runtime-probe.lock')) {
        throw 'Another probe owns runtime-probe.lock; benchmarks must run sequentially.'
    }
    if (Test-Path -LiteralPath (Join-Path $analysisTask "$Name-stop.flag")) {
        throw 'Verification stop flag requested cancellation.'
    }
    $activeTask = @(Get-Process -Name 'brute_force_e3*' -ErrorAction SilentlyContinue)
    if ($activeTask.Count) { throw 'Another Brute Force process is active; benchmarks must run sequentially.' }
    $settingsTask = @{ Name=$ProbeName; Executable=$Executable; Counters=$true }
    if ($Experiment -eq 'Overhead' -and -not $Enabled) {
        $settingsTask.Executable=$BaselineExecutable
        $settingsTask.Counters=$false
    }
    if ($Enabled) {
        switch ($Experiment) {
            'Release' { $settingsTask.ReleaseCoalesce=$true }
            'TextureHash' { $settingsTask.TextureHash=$true }
            'TextureEpoch' { $settingsTask.TextureEpoch=$true }
            'TextureCombined' {
                $settingsTask.TextureHash=$true
                $settingsTask.TextureEpoch=$true
            }
            'Combined' {
                $settingsTask.ReleaseCoalesce=$true
                $settingsTask.TextureHash=$true
                $settingsTask.TextureEpoch=$true
            }
        }
    }
    $secondsTask = if ($Replay) { 2000 } else { 700 }
    if ($Replay) { $settingsTask.NormalGraphics=$true; $settingsTask.PausedReplay=$true }
    else { $settingsTask.ProductionBenchmark=$true }
    $settingsTask.Seconds=$secondsTask
    $stateDataTask.current=$ProbeName;Save-StateTask
    $recordersTask=@()
    try {
        if (-not $Replay) {
            foreach ($recorderTask in @('monitor_gpu.ps1','monitor_nvidia.ps1')) {
                $scriptTask=Join-Path $PSScriptRoot $recorderTask
                $recordersTask += Start-Process $recorderShellTask -WindowStyle Hidden -PassThru -ArgumentList @(
                    '-NoProfile','-File',$scriptTask,'-Name',$ProbeName,'-Seconds',($secondsTask+40))
            }
        }
        & (Join-Path $PSScriptRoot 'performance.ps1') @settingsTask
        $resultTask=Get-Content -LiteralPath (Join-Path $analysisTask "$ProbeName.json") -Raw | ConvertFrom-Json
        if ($resultTask.timed_out -or $resultTask.stopped -or $resultTask.exit_code -ne 3) {
            throw "Probe $ProbeName did not finish normally; inspect its logs and guest snapshot."
        }
        if (-not $Replay) {
            $reportTask=Get-Content -LiteralPath (Join-Path $analysisTask "$ProbeName-performance.json") -Raw | ConvertFrom-Json
            if (@($reportTask.windows | Where-Object { $null -eq $_.mean_ms_per_frame }).Count) {
                throw "Probe $ProbeName has incomplete performance windows; do not treat its watchdog exit as route success."
            }
            foreach ($recorderProcessTask in $recordersTask) {
                if ($recorderProcessTask.HasExited -and $recorderProcessTask.ExitCode -ne 0) {
                    throw "A hardware recorder failed for $ProbeName; inspect telemetry before accepting the run."
                }
            }
            if (-not (Test-Path -LiteralPath (Join-Path $analysisTask "$ProbeName-nvidia.jsonl")) -or
                -not @(Get-ChildItem -LiteralPath $analysisTask -Filter "$ProbeName*-gpu-load.jsonl").Count) {
                throw "Hardware telemetry is missing for $ProbeName; the comparison is incomplete."
            }
        }
        $stateDataTask.completed += $ProbeName;Save-StateTask
    } finally {
        foreach ($recorderProcessTask in $recordersTask) {
            if (-not $recorderProcessTask.HasExited) {
                if (-not $recorderProcessTask.WaitForExit(10000)) { $recorderProcessTask.Kill() }
            }
            $recorderProcessTask.Dispose()
        }
    }
}
try {
    Save-StateTask
    if ($ResumeComparison) {
        if (-not $ReplayGates -or $Experiment -eq 'Overhead') { throw 'ResumeComparison requires optimization ReplayGates.' }
        $comparisonDataTask=Get-Content -LiteralPath (Join-Path $analysisTask "$ResumeComparison.json") -Raw | ConvertFrom-Json
        $evidenceTask=$comparisonDataTask.repeated_runs
        if ($evidenceTask.pairs -lt 3 -or -not $evidenceTask.performance_threshold_passed -or
            -not $evidenceTask.work_equal_within_one_percent -or -not $evidenceTask.executor_cpu_reduction_consistent) {
            throw 'Resume evidence has not passed the repeated performance gate.'
        }
        $hashTask=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot "build/RelWithDebInfo/$Executable") -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedTask=@{ RECOMP_HW_RELEASE_COALESCE=$null; RECOMP_HW_RELEASE_BARRIER=$null; RECOMP_HW_TEXTURE_HASH=$null; RECOMP_HW_TEXTURE_EPOCH=$null }
        switch ($Experiment) {
            'Release' { $expectedTask.RECOMP_HW_RELEASE_COALESCE='1' }
            'TextureHash' { $expectedTask.RECOMP_HW_TEXTURE_HASH='1' }
            'TextureEpoch' { $expectedTask.RECOMP_HW_TEXTURE_EPOCH='1' }
            'TextureCombined' { $expectedTask.RECOMP_HW_TEXTURE_HASH='1';$expectedTask.RECOMP_HW_TEXTURE_EPOCH='1' }
            'Combined' { $expectedTask.RECOMP_HW_RELEASE_COALESCE='1';$expectedTask.RECOMP_HW_TEXTURE_HASH='1';$expectedTask.RECOMP_HW_TEXTURE_EPOCH='1' }
        }
        foreach ($armTask in @('baselines','candidates')) {
            if (@($evidenceTask.$armTask).Count -ne $evidenceTask.pairs) { throw 'Resume evidence is incomplete.' }
            foreach ($runTask in $evidenceTask.$armTask) {
                if ($runTask.sha256 -ne $hashTask -or $runTask.sha256_at_end -ne $hashTask -or
                    $runTask.terminal.exit_code -ne 3 -or $runTask.terminal.stopped -or $runTask.terminal.timed_out) {
                    throw 'Resume evidence does not match this binary or normal termination.'
                }
                $configTask=(Get-Content -LiteralPath (Join-Path $analysisTask "$($runTask.name)-process.json") -Raw | ConvertFrom-Json).configuration
                foreach ($keyTask in $expectedTask.Keys) {
                    $valueTask=if ($armTask -eq 'candidates') { $expectedTask[$keyTask] } else { $null }
                    if ($configTask.$keyTask -ne $valueTask) { throw 'Resume evidence uses different experiment flags.' }
                }
            }
        }
    } else {
    $comparisonTask=@()
    for ($pairTask=1; $pairTask -le $Pairs; $pairTask++) {
        $offTask="${Name}_off_$pairTask"; $onTask="${Name}_on_$pairTask"
        Invoke-ProbeTask $offTask $false $false
        Invoke-ProbeTask $onTask $true $false
        & python (Join-Path $PSScriptRoot 'compare_performance.py') --baseline $offTask --candidate $onTask --name "$Name-pair-$pairTask"
        if ($LASTEXITCODE -ne 0) { throw 'Pair comparison failed.' }
        $pairDataTask=Get-Content -LiteralPath (Join-Path $analysisTask "$Name-pair-$pairTask.json") -Raw | ConvertFrom-Json
        foreach ($workKeyTask in @('program_batches','program_vertices')) {
            $beforeWorkTask=$pairDataTask.baseline.vertex_work_per_frame.$workKeyTask
            $afterWorkTask=$pairDataTask.candidate.vertex_work_per_frame.$workKeyTask
            if ($null -eq $beforeWorkTask -or $null -eq $afterWorkTask -or $beforeWorkTask -le 0 -or
                [Math]::Abs($afterWorkTask/$beforeWorkTask-1) -gt 0.01) {
                throw "Pair $pairTask has different guest work; inspect route captures and stall signatures before retrying."
            }
        }
        $comparisonTask += @('--baseline',$offTask,'--candidate',$onTask)
    }
    & python (Join-Path $PSScriptRoot 'compare_performance.py') @comparisonTask --name "$Name-comparison"
    if ($LASTEXITCODE -ne 0) { throw 'Repeated comparison failed.' }
    $comparisonDataTask=Get-Content -LiteralPath (Join-Path $analysisTask "$Name-comparison.json") -Raw | ConvertFrom-Json
    }
    if ($Experiment -ne 'Overhead' -and -not (
        $comparisonDataTask.repeated_runs.performance_threshold_passed -and
        $comparisonDataTask.repeated_runs.work_equal_within_one_percent -and
        $comparisonDataTask.repeated_runs.executor_cpu_reduction_consistent -and
        ($ResumeComparison -or ($comparisonDataTask.repeated_runs.hardware_observation_complete -and
         (($ConcurrentWorkload -and $comparisonDataTask.repeated_runs.concurrent_context_comparable) -or
          (-not $ConcurrentWorkload -and $comparisonDataTask.repeated_runs.low_competing_gpu_load)))))) {
        $stateDataTask.status='performance_gate_failed';$stateDataTask.current=$null;Save-StateTask
        return
    }
    if ($ReplayGates) {
        for ($replayTask=1; $replayTask -le 2; $replayTask++) {
            Invoke-ProbeTask "${Name}_replay_off_$replayTask" $false $true
            Invoke-ProbeTask "${Name}_replay_on_$replayTask" $true $true
        }
    }
    $stateDataTask.status=if ($ConcurrentWorkload) { 'awaiting_concurrent_workload_review' } else { 'awaiting_capture_and_correctness_review' }
    $stateDataTask.current=$null;Save-StateTask
} catch {
    $stateDataTask.status='failed';$stateDataTask['error']=$_.Exception.Message;Save-StateTask
    throw
}
