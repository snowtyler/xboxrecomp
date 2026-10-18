$ErrorActionPreference='Stop'
$originalTask=[Environment]::GetEnvironmentVariables('Process')
$captureTask=[pscustomobject]@{Environment=$null;Arguments=$null}
# Intercept the native launch boundary; this test never creates a game.
function python {
    $captureTask.Environment=[Environment]::GetEnvironmentVariables('Process')
    $captureTask.Arguments=@($args)
    $global:LASTEXITCODE=0
}
try {
    foreach($keyTask in @('RECOMP_FB_DUMP','RECOMP_FB_DUMP_FLIPS','RECOMP_FB_DUMP_GUEST_FLIPS')) {
        Set-Item "Env:$keyTask" '1'
    }
    & (Join-Path $PSScriptRoot 'probe_runtime.ps1') -Name fixture_lean_no_launch `
        -Executable fixture.exe -Seconds 20 -NoSurfaceDumps -SampleFrom -1 -Measure
    foreach($keyTask in @('RECOMP_FB_DUMP','RECOMP_FB_DUMP_FLIPS','RECOMP_FB_DUMP_GUEST_FLIPS')) {
        if($captureTask.Environment.Contains($keyTask)){throw "Dump flag leaked: $keyTask"}
    }
    $indexTask=[Array]::IndexOf($captureTask.Arguments,'--sample-from')
    if($indexTask -lt 0 -or $captureTask.Arguments[$indexTask+1] -ne '-1'){throw 'Sampler was not disabled.'}
    $rejectedTask=$false
    try {
        & (Join-Path $PSScriptRoot 'probe_runtime.ps1') -Name fixture_bad_no_launch `
            -Executable fixture.exe -NoSurfaceDumps -Captures '10:1'
    } catch {$rejectedTask=$_.Exception.Message -like '*cannot be combined*'}
    if(-not $rejectedTask){throw 'Conflicting dump/capture options were accepted.'}
    Write-Output 'Lean launch boundary passed: no surface dumps, sampler disabled, conflicting captures rejected.'
} finally {
    foreach($keyTask in @([Environment]::GetEnvironmentVariables('Process').Keys)) {
        if($keyTask -match '^(RECOMP_|BRUTE_FORCE_E3_)') {
            if($originalTask.Contains($keyTask)){Set-Item "Env:$keyTask" $originalTask[$keyTask]}
            else{Remove-Item "Env:$keyTask" -ErrorAction SilentlyContinue}
        }
    }
}
