param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [string]$Executable='brute_force_e3_perf150.exe',
    [ValidateRange(1,3)][int]$Pairs=3,
    [ValidateRange(160,300)][int]$Seconds=180,
    [switch]$NormalBarriers
)
$ErrorActionPreference='Stop'
$analysisTask=Join-Path $PSScriptRoot 'analysis'
$statePathTask=Join-Path $analysisTask "$Name-irq-verification.json"
if(Test-Path $statePathTask){throw 'Workflow name already exists.'}
$stateTask=@{status='running';current=$null;completed=@();normal_barriers=[bool]$NormalBarriers;executable=$Executable}
function Save-StateTask {$stateTask|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $statePathTask}
try {
 Save-StateTask
 for($pairTask=1;$pairTask -le $Pairs;$pairTask++) {
  foreach($gateTask in @($false,$true)) {
   if(Test-Path (Join-Path $analysisTask "$Name-stop.flag")){throw 'Workflow cancelled.'}
   if(Test-Path (Join-Path $analysisTask 'runtime-probe.lock')){throw 'Another probe is active.'}
   if(@(Get-Process -Name 'brute_force_e3*' -ErrorAction SilentlyContinue).Count){throw 'A Brute Force process is already running.'}
   $armTask=if($gateTask){'gate'}else{'control'}
   $probeTask="${Name}_${armTask}_$pairTask"
   $stateTask.current=$probeTask;Save-StateTask
   & (Join-Path $PSScriptRoot 'performance.ps1') -Name $probeTask -Executable $Executable -Seconds $Seconds `
     -Counters -OrderTrace -TitleProbe -IrqGate:$gateTask -ReleaseCoalesce:(-not $NormalBarriers)
   $resultTask=Get-Content (Join-Path $analysisTask "$probeTask.json") -Raw|ConvertFrom-Json
   if($resultTask.exit_code -ne 3 -or $resultTask.stopped -or $resultTask.timed_out){throw 'Probe did not finish normally.'}
   $observerTask=Get-Content (Join-Path $analysisTask "$probeTask-order.json") -Raw|ConvertFrom-Json
   if($observerTask.records -lt 1 -or $observerTask.discarded -or $observerTask.overwritten -or $observerTask.read_error){throw 'Incomplete order trace; inspect observer metadata.'}
   $stateTask.completed+= $probeTask;Save-StateTask
  }
 }
 $stateTask.status='awaiting_timeline_and_capture_review';$stateTask.current=$null;Save-StateTask
} catch {$stateTask.status='failed';$stateTask.error=$_.Exception.Message;Save-StateTask;throw}
