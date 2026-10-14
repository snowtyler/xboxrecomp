param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [int]$Seconds = 730,
    [int]$IntervalSeconds = 5
)
$ErrorActionPreference = 'Stop'
$analysisTask = Join-Path $PSScriptRoot 'analysis'
$outputTask = Join-Path $analysisTask "$Name-gpu-load.jsonl"
if (Test-Path -LiteralPath $outputTask) { throw 'GPU observation exists; use a fresh name.' }
$deadlineTask = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadlineTask) {
    # Engine instances can disappear while Get-Counter samples them. Preserve
    # valid samples and record errors rather than silently ending observation.
    $counterErrorsTask = @()
    $counterTask = Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue -ErrorVariable counterErrorsTask
    $enginesTask = @($counterTask.CounterSamples | Where-Object {
        $_.Status -eq 0 -and $_.InstanceName -match '^pid_[0-9]+_.*engtype_3d$' -and $_.CookedValue -gt 0.1
    } | ForEach-Object {
        [ordered]@{ instance=$_.InstanceName; percent=[Math]::Round($_.CookedValue,4) }
    })
    [ordered]@{ utc=(Get-Date).ToUniversalTime().ToString('o'); engines=$enginesTask; counter_errors=@($counterErrorsTask | ForEach-Object { $_.ToString() }) } |
        ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $outputTask
    if (Test-Path -LiteralPath (Join-Path $analysisTask "$Name.json")) { break }
    Start-Sleep -Seconds $IntervalSeconds
}
Write-Output $outputTask
