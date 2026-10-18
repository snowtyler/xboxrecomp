param(
    [Parameter(Mandatory=$true)][string]$Name,
    [Parameter(Mandatory=$true)][string]$Executable,
    [int]$Seconds = 420,
    [switch]$Audio,
    [string]$DspDoorbell,
    [switch]$Vertices,
    [double]$CaptureFrom = -1,
    [double]$CaptureFor = 20,
    [string]$Captures = '',
    [double]$SampleFrom = -1,
    [double]$SampleFor = 10,
    [double]$InspectFrom = -1,
    [double]$StateInterval = 0,
    [switch]$Measure,
    [string]$PadScript = '5000:start:400,8000:a:400,12000:down:400,16000:a:400'
)
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Use a simple probe name.' }
$analysisTask = Join-Path $PSScriptRoot 'analysis'
$env:BRUTE_FORCE_E3_FENCE_MIRROR = '1'
$env:RECOMP_PB_EXEC = '1'
$env:RECOMP_VBLANK = '1'
$env:RECOMP_TITLE_KEVENTS = '1'
$env:RECOMP_USB = '1'
$env:RECOMP_USB_HC = '0'
$env:RECOMP_USB_NDP = '4'
$env:RECOMP_USB_PORT = '2'
$env:RECOMP_AC97_READY = if ($Audio) { '1' } else { $null }
if ($DspDoorbell) { $env:RECOMP_APU_DSP_ACK = $DspDoorbell }
$env:RECOMP_STUB_TRACE = '1'
$env:RECOMP_PB_REPORT_MS = '2000'
$env:RECOMP_FB_DUMP = Join-Path $analysisTask "$Name-frame-"
$env:RECOMP_PAD_LIVE = Join-Path $analysisTask "$Name-live.txt"
$env:RECOMP_FRAME_TRACE = Join-Path $analysisTask "$Name-trace.flag"
$env:RECOMP_FRAME_TRACE_VERTICES = if ($Vertices) { '1' } else { $null }
$env:RECOMP_PAD_SCRIPT = $PadScript
[string[]]$measureTask = @()
if ($Measure) { $measureTask = @('--measure') }
& python (Join-Path $PSScriptRoot 'probe.py') --seconds $Seconds --name $Name --exe $Executable --stop-file (Join-Path $analysisTask "$Name-stop.flag") --capture-from $CaptureFrom --capture-for $CaptureFor --captures $Captures --sample-from $SampleFrom --sample-for $SampleFor --inspect-from $InspectFrom --state-interval $StateInterval @measureTask
if ($LASTEXITCODE -ne 0) { throw 'Probe runner failed.' }
