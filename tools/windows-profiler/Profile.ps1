[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('start','status','stop','shutdown','cpu3','cpu30','gpu3','gpu30','combat30')][string]$Operation)
$ErrorActionPreference='Stop'
$rootTask='C:\ProgramData\BruteForceProfiler'
$exeTask=Join-Path $PSScriptRoot '..\..\games\brute_force_e3\analysis\profiler-helper-build\Client.exe'
if(-not(Test-Path -LiteralPath $exeTask)){throw 'Profiling task is not installed'}
if($Operation -eq 'start'){
 Start-ScheduledTask -TaskName 'BruteForceProfiler-Collector'
 $deadlineTask=(Get-Date).AddSeconds(15)
 do {
  Start-Sleep -Milliseconds 250
  $receiptTask=Join-Path $rootTask 'broker.json'
  if(Test-Path -LiteralPath $receiptTask){
   $statusTask=Get-Content -LiteralPath $receiptTask -Raw|ConvertFrom-Json
   if($statusTask.status -in @('ready','busy')){
    & $exeTask status
    if($LASTEXITCODE -eq 0){exit 0}
   }
  }
 }while((Get-Date)-lt $deadlineTask)
 throw 'Task did not become ready; inspect Task Scheduler and installation receipt'
}
& $exeTask $Operation
exit $LASTEXITCODE
