#Requires -Version 7.0
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [Parameter(Mandatory=$true)][ValidatePattern('^brute_force_e3_candidate[0-9]+\.exe$')][string]$Executable,
    [ValidateRange(1500,1800)][int]$Seconds = 1500
)
$ErrorActionPreference='Stop'
$binaryTask=Join-Path $PSScriptRoot "build/RelWithDebInfo/$Executable"
$pdbTask=[IO.Path]::ChangeExtension($binaryTask,'.pdb')
if(-not (Test-Path -LiteralPath $binaryTask -PathType Leaf) -or -not (Test-Path -LiteralPath $pdbTask -PathType Leaf)) {
    throw 'Both the candidate executable and its matching PDB are required'
}
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'analysis/runtime-probe.lock')) {
    throw 'Another probe is active; wait for it before starting this gate'
}
$saveTask=Join-Path $PSScriptRoot "analysis/$Name-saves"
if(Test-Path -LiteralPath $saveTask){throw 'Use a fresh gate name/save directory'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'saves') -Destination $saveTask -Recurse
$manifestTask=@{
    executable=$Executable
    exe_sha256=(Get-FileHash -LiteralPath $binaryTask -Algorithm SHA256).Hash
    pdb_sha256=(Get-FileHash -LiteralPath $pdbTask -Algorithm SHA256).Hash
    seconds=$Seconds
    save_directory=$saveTask
    policy='normal barriers; default GET progress with monitor; IRQ/coalescing/render diagnostics/epochs off'
    status='running'
}
$fileTask=Join-Path $PSScriptRoot "analysis/$Name-integration-gate.json"
$savedTask=@{}
$settingsTask=@{RECOMP_NV2A_COLOR_CLAMP=$null;RECOMP_NV2A_V015_RENDER=$null;BRUTE_FORCE_E3_SAVE_DIR=$saveTask}
foreach($flagTask in $settingsTask.Keys) {
    $savedTask[$flagTask]=[Environment]::GetEnvironmentVariable($flagTask,'Process')
    if($null -eq $settingsTask[$flagTask]) {Remove-Item -LiteralPath "Env:$flagTask" -ErrorAction SilentlyContinue}
    else {Set-Item -LiteralPath "Env:$flagTask" -Value $settingsTask[$flagTask]}
}
try {
    $manifestTask|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $fileTask
    & (Join-Path $PSScriptRoot 'verify_upstream_v014.ps1') -Name $Name -Executable $Executable `
        -Seconds $Seconds -Replay -UseDefaultGetProgress -GuestStateEvidence
    if($LASTEXITCODE -ne 0){throw 'Integration gameplay gate failed'}
    $resultTask=Get-Content -LiteralPath (Join-Path $PSScriptRoot "analysis/$Name.json") -Raw|ConvertFrom-Json
    if($resultTask.elapsed_seconds -lt ($Seconds-2) -or $resultTask.timed_out -or $resultTask.stopped) {
        throw 'Required full gate duration was not completed normally under the watchdog'
    }
    $manifestTask.elapsed_seconds=$resultTask.elapsed_seconds
    $manifestTask.status='passed'
} catch {
    $manifestTask.status='failed'
    $manifestTask.error=$_.Exception.Message
    throw
} finally {
    $manifestTask.finished_utc=(Get-Date).ToUniversalTime().ToString('o')
    $manifestTask|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $fileTask
    foreach($flagTask in $savedTask.Keys) {
        if($null -eq $savedTask[$flagTask]) {Remove-Item -LiteralPath "Env:$flagTask" -ErrorAction SilentlyContinue}
        else {Set-Item -LiteralPath "Env:$flagTask" -Value $savedTask[$flagTask]}
    }
}
