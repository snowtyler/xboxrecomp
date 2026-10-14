param([string]$Configuration = 'RelWithDebInfo', [string]$OutputName = 'brute_force_e3_recomp', [switch]$AbiCheck, [switch]$Optimize)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC 2022 C++ build tools are required.' }
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
$cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$project = $PSScriptRoot
if ($OutputName -notmatch '^[A-Za-z0-9_-]+$') { throw 'Use a simple executable basename.' }
$probeLockTask = Join-Path $project 'analysis/runtime-probe.lock'
if (Test-Path -LiteralPath $probeLockTask) {
    $probeOwnerTask = Get-Content -LiteralPath $probeLockTask -Raw | ConvertFrom-Json
    if ($probeOwnerTask.executable -eq "$OutputName.exe") {
        throw "Cannot rebuild $OutputName while probe $($probeOwnerTask.name) owns it."
    }
}
$activeTask = @(Get-Process -Name $OutputName -ErrorAction SilentlyContinue)
if ($activeTask.Count) { throw "Cannot rebuild $OutputName while its process is running." }
if ($OutputName -in @('brute_force_e3_prototype', 'brute_force_e3_perf132')) {
    throw 'Verified and retained candidate basenames are protected; select a new OutputName.'
}
$build = Join-Path $project 'build'
$abiOption = if ($AbiCheck) { 'ON' } else { 'OFF' }
$optimizeOption = if ($Optimize) { 'ON' } else { 'OFF' }
& $cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_GENERATOR_INSTANCE=$installation" "-DBRUTE_FORCE_E3_OUTPUT_NAME=$OutputName" "-DBRUTE_FORCE_E3_ABI_CHECK=$abiOption" "-DBRUTE_FORCE_E3_OPTIMIZE=$optimizeOption" '-DCMAKE_SUPPRESS_REGENERATION=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
# Use the MSBuild belonging to the selected compiler installation. On this
# machine CMake's default selected MSBuild 18 for a VS 17 project and stalled.
& $msbuild (Join-Path $build 'brute_force_e3_recomp.vcxproj') "/p:Configuration=$Configuration" /p:Platform=x64 /p:MultiProcessorCompilation=true /p:CL_MPCount=4 /m:1 /nr:false /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
