param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
    [int]$Seconds = 740,
    [int]$IntervalSeconds = 5
)
$ErrorActionPreference = 'Stop'
$analysisTask = Join-Path $PSScriptRoot 'analysis'
$outputTask = Join-Path $analysisTask "$Name-nvidia.jsonl"
if (Test-Path -LiteralPath $outputTask) { throw 'NVIDIA observation exists; use a fresh name.' }
$commandTask = (Get-Command nvidia-smi -ErrorAction Stop).Source
$deadlineTask = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadlineTask) {
    $utcTask = (Get-Date).ToUniversalTime().ToString('o')
    $rowsTask = @(& $commandTask '--query-gpu=name,utilization.gpu,utilization.memory,clocks.gr,clocks.mem,temperature.gpu,power.draw' '--format=csv,noheader,nounits' 2>&1 | ForEach-Object { $_.ToString() })
    $exitTask = $LASTEXITCODE
    [ordered]@{ utc=$utcTask; exit_code=$exitTask; fields=@('name','gpu_percent','memory_percent','graphics_mhz','memory_mhz','temperature_c','power_w'); csv=$rowsTask } |
        ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $outputTask
    if (Test-Path -LiteralPath (Join-Path $analysisTask "$Name.json")) { break }
    Start-Sleep -Seconds $IntervalSeconds
}
Write-Output $outputTask
