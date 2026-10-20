[CmdletBinding()]
param([switch]$CombinedOnly)
$ErrorActionPreference='Stop'
$identityTask=[Security.Principal.WindowsIdentity]::GetCurrent()
if(([Security.Principal.WindowsPrincipal]$identityTask).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run validation from the ordinary non-elevated user token'}
$rootTask='C:\ProgramData\BruteForceProfiler'
$exeTask=Join-Path $PSScriptRoot '..\..\games\brute_force_e3\analysis\profiler-helper-build\Client.exe'
$outTask=Join-Path $PSScriptRoot ('..\..\games\brute_force_e3\analysis\profiler-helper-validation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
[void](New-Item -ItemType Directory -Path $outTask)
$resultsTask=@{non_elevated=$true;started_utc=(Get-Date).ToUniversalTime().ToString('o');checks=@();jobs=@();status='running'}
function Invoke-RequestTask([string]$Operation){
 $rawTask=& $exeTask $Operation
 if($LASTEXITCODE -ne 0){throw "Request failed: $Operation"}
 return (($rawTask -join "`n")|ConvertFrom-Json)
}
function Wait-JobTask([string]$Id,[string]$Expected){
 $deadlineTask=(Get-Date).AddSeconds(180)
 do {
  Start-Sleep -Milliseconds 500
  $stateTask=Invoke-RequestTask status
  if($stateTask.last_job -eq $Id){
   if($stateTask.last_result.status -ne $Expected){throw "Unexpected result for $Id : $($stateTask.last_result.status)"}
   $destinationTask=Join-Path $outTask $Id
   Copy-Item -LiteralPath (Join-Path $rootTask "recordings\$Id") -Destination $destinationTask -Recurse
   return $stateTask.last_result
  }
 }while((Get-Date)-lt $deadlineTask)
 throw "Timed out waiting for $Id"
}
try {
 foreach($fileTask in @('Profiler.exe','Launcher.exe','config.json')){
  $pathTask=Join-Path $rootTask $fileTask
  if((Get-Acl -LiteralPath $pathTask).GetOwner([Security.Principal.SecurityIdentifier]).Value -ne 'S-1-5-32-544'){throw "Wrong protected owner: $fileTask"}
  $deniedTask=$false
  try {$streamTask=[IO.File]::Open($pathTask,'Open','Write','ReadWrite');$streamTask.Dispose()}catch [UnauthorizedAccessException]{$deniedTask=$true}
  if(-not $deniedTask){throw "Ordinary user can write privileged file: $fileTask"}
 }
 $probeTask=Join-Path $rootTask ('unauthorized-'+[guid]::NewGuid().ToString('N'))
 $deniedTask=$false
 try {$streamTask=[IO.File]::Open($probeTask,'CreateNew','Write','ReadWrite');$streamTask.Dispose();Remove-Item -LiteralPath $probeTask}catch [UnauthorizedAccessException]{$deniedTask=$true}
 if(-not $deniedTask){throw 'Ordinary user can create privileged files'}
 $serviceTask=New-Object -ComObject Schedule.Service;$serviceTask.Connect()
 $taskTask=$serviceTask.GetFolder('\').GetTask('BruteForceProfiler-Collector')
 $sddlTask=$taskTask.GetSecurityDescriptor(7)
 $deniedTask=$false
 try {$taskTask.SetSecurityDescriptor($sddlTask,0)}catch {if($_.Exception.HResult -eq -2147024891 -or $_.Exception.Message -match '0x80070005|Access is denied'){$deniedTask=$true}else{throw}}
 if(-not $deniedTask){throw 'Ordinary user can change elevated task permissions'}
 $resultsTask.checks+='file/directory writes denied; owners Administrators; task DACL changes denied'
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\Profile.ps1" start > (Join-Path $outTask 'start.json')
 if($LASTEXITCODE -ne 0){throw 'Unattended task start failed'}
 $stateTask=Invoke-RequestTask status
 $beforeTask=$stateTask.captures_started
 $sidTask=$identityTask.User.Value
 $pipeTask=[IO.Pipes.NamedPipeClientStream]::new('.','BruteForceProfiler-v1-'+$sidTask,[IO.Pipes.PipeAccessRights]::ReadWrite,[IO.Pipes.PipeOptions]::Asynchronous,[Security.Principal.TokenImpersonationLevel]::Identification,[IO.HandleInheritability]::None)
 try {
  $pipeTask.Connect(3000)
  $bytesTask=[Text.Encoding]::ASCII.GetBytes("cpu3 & whoami`n")
  $responseTask=New-Object byte[] 4096
  $readTask=$pipeTask.ReadAsync($responseTask,0,$responseTask.Length)
  $pipeTask.Write($bytesTask,0,$bytesTask.Length)
  if(-not $readTask.Wait(5000)){throw 'Invalid request rejection timed out'}
  $responseTextTask=[Text.Encoding]::UTF8.GetString($responseTask,0,$readTask.Result)
  if($responseTextTask -notmatch 'Request rejected'){throw 'Expected explicit unsafe-request rejection was not received'}
 }finally{$pipeTask.Dispose()}
 if((Invoke-RequestTask status).captures_started -ne $beforeTask){throw 'Rejected request started a capture'}
 $resultsTask.checks+='backend rejects injected operation without starting work'
 $modesTask=if($CombinedOnly){@('combat30')}else{@('cpu3','gpu3')}
 foreach($modeTask in $modesTask){
  $startedTask=Invoke-RequestTask $modeTask
  if($CombinedOnly){
   $rejectedTask=& $exeTask cpu3
   if($LASTEXITCODE -ne 1 -or ($rejectedTask -join '') -notmatch 'Request rejected'){throw 'Busy broker accepted another capture'}
   if((Invoke-RequestTask status).captures_started -ne 1){throw 'Busy rejection changed capture count'}
   $resultsTask.checks+='busy broker rejects overlapping capture'
  }
  $resultTask=Wait-JobTask $startedTask.current_job 'completed'
  $resultsTask.jobs+=@{id=$startedTask.current_job;mode=$modeTask;result=$resultTask}
 }
 if(-not $CombinedOnly){
 $startedTask=Invoke-RequestTask cpu30
 $deadlineTask=(Get-Date).AddSeconds(20)
 do {
  Start-Sleep -Milliseconds 200
  $stateTask=Invoke-RequestTask status
  if($stateTask.current_result.status -eq 'recording'){break}
 }while((Get-Date)-lt $deadlineTask)
 if($stateTask.current_result.status -ne 'recording'){throw 'Cancellation test did not reach recording'}
 $null=Invoke-RequestTask stop
 $resultTask=Wait-JobTask $startedTask.current_job 'cancelled'
 $resultsTask.jobs+=@{id=$startedTask.current_job;mode='cpu30-stop';result=$resultTask}
 $resultsTask.checks+='CPU and NVIDIA metrics smoke captures completed; live CPU cancellation salvaged ETL'
 }else{$resultsTask.checks+='combined CPU/NVIDIA capture completed after unattended session restart'}
 $resultsTask.status='passed'
}catch{$resultsTask.status='failed';$resultsTask.error=$_.Exception.Message;throw}
finally {
 try {
  $null=Invoke-RequestTask shutdown
  $deadlineTask=(Get-Date).AddSeconds(180)
  do {Start-Sleep -Milliseconds 250;$brokerTask=Get-Content (Join-Path $rootTask 'broker.json') -Raw|ConvertFrom-Json}while($brokerTask.status -ne 'stopped' -and (Get-Date)-lt $deadlineTask)
  $resultsTask.broker_stopped=($brokerTask.status -eq 'stopped')
  if(-not $resultsTask.broker_stopped){$resultsTask.status='failed';$resultsTask.shutdown_error='Broker did not stop within deadline'}
 }catch{$resultsTask.shutdown_error=$_.Exception.Message}
 $resultsTask.finished_utc=(Get-Date).ToUniversalTime().ToString('o')
 $resultsTask|ConvertTo-Json -Depth 8|Set-Content (Join-Path $outTask 'validation.json')
 Write-Output "Validation receipt: $outTask\validation.json"
}
