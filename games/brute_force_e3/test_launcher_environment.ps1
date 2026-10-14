$ErrorActionPreference = 'Stop'
$disabledTask = @('RECOMP_AUDIO_MUTE','RECOMP_NV2A_PFB_POLL','RECOMP_HW_RELEASE_COALESCE',
                  'RECOMP_HW_RELEASE_BARRIER','RECOMP_HW_TEXTURE_HASH','RECOMP_HW_GPU_TRACE',
                  'RECOMP_GPU_TIMING','RECOMP_HW_COUNTERS')
$originalTask = @{}
$captureHolderTask = [pscustomobject]@{ Environment=$null }
foreach ($keyTask in $disabledTask + @('RECOMP_HW_TEXTURE_EPOCH')) {
    $originalTask[$keyTask] = [Environment]::GetEnvironmentVariable($keyTask)
}
# Intercept only this test's launch: inspect the exact environment at the child
# creation boundary without creating a game or changing its binaries.
function Start-Process {
    param($FilePath,$WorkingDirectory,$WindowStyle,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
    $captureHolderTask.Environment = [Environment]::GetEnvironmentVariables('Process')
    $processTask = [pscustomobject]@{ HasExited=$true; ExitCode=0 }
    $processTask | Add-Member ScriptMethod WaitForExit {}
    $processTask | Add-Member ScriptMethod Dispose {}
    return $processTask
}
try {
    foreach ($keyTask in $disabledTask) { Set-Item -LiteralPath "Env:$keyTask" -Value '1' }
    Remove-Item -LiteralPath Env:RECOMP_HW_TEXTURE_EPOCH -ErrorAction SilentlyContinue
    & (Join-Path $PSScriptRoot 'play.ps1') -Executable brute_force_e3_perf149.exe -TextureEpoch
    foreach ($keyTask in $disabledTask) {
        if ($captureHolderTask.Environment.Contains($keyTask)) { throw "Disabled flag leaked into launch: $keyTask" }
        if ([Environment]::GetEnvironmentVariable($keyTask) -ne '1') { throw "Caller environment not restored: $keyTask" }
    }
    if ($captureHolderTask.Environment['RECOMP_HW_TEXTURE_EPOCH'] -ne '1') { throw 'Explicit epoch option missing.' }
    if ([Environment]::GetEnvironmentVariables().Contains('RECOMP_HW_TEXTURE_EPOCH')) { throw 'Absent caller flag was restored as empty.' }
    foreach ($keyTask in $disabledTask) { Remove-Item -LiteralPath "Env:$keyTask" }
    & (Join-Path $PSScriptRoot 'play.ps1') -Executable brute_force_e3_perf149.exe
    foreach ($keyTask in $disabledTask + @('RECOMP_HW_TEXTURE_EPOCH')) {
        if ($captureHolderTask.Environment.Contains($keyTask) -or [Environment]::GetEnvironmentVariables().Contains($keyTask)) {
            throw "Default flag is present rather than absent: $keyTask"
        }
    }
    Write-Output 'Launcher environment checks passed: disabled flags absent, explicit epoch enabled, caller environment restored.'
} finally {
    foreach ($keyTask in $originalTask.Keys) {
        if ($null -eq $originalTask[$keyTask]) { Remove-Item -LiteralPath "Env:$keyTask" -ErrorAction SilentlyContinue }
        else { Set-Item -LiteralPath "Env:$keyTask" -Value $originalTask[$keyTask] }
    }
}
