[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$outputTask=Join-Path $PSScriptRoot '..\..\games\brute_force_e3\analysis\profiler-helper-build'
[void](New-Item -ItemType Directory -Path $outputTask -Force)
$compilerTask=Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compilerTask /nologo /target:exe /platform:x64 /optimize+ /reference:System.Web.Extensions.dll "/out:$outputTask\Profiler.exe" "$PSScriptRoot\Profiler.cs"
if($LASTEXITCODE -ne 0){throw 'Profiler helper compilation failed'}
& "$outputTask\Profiler.exe" selftest
if($LASTEXITCODE -ne 0){throw 'Protocol tests failed'}
$vswhereTask='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsTask=(& $vswhereTask -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if(-not $vsTask){throw 'MSVC x64 tools are required for the native launcher'}
$vcvarsTask=Join-Path $vsTask 'VC\Auxiliary\Build\vcvars64.bat'
$compileTask='call "'+$vcvarsTask+'" >nul && cl.exe /nologo /W4 /O2 /MT /DUNICODE /D_UNICODE "'+$PSScriptRoot+'\Launcher.c" /Fo"'+$outputTask+'\Launcher.obj" /Fe"'+$outputTask+'\Launcher.exe" /link /SUBSYSTEM:WINDOWS'
& $env:ComSpec /d /c $compileTask
if($LASTEXITCODE -ne 0){throw 'Native launcher compilation failed'}
$hashTask=(Get-FileHash -LiteralPath "$outputTask\Profiler.exe" -Algorithm SHA256).Hash
@{sha256=$hashTask;launcher_sha256=(Get-FileHash -LiteralPath "$outputTask\Launcher.exe").Hash;binary="$outputTask\Profiler.exe";source_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot\Profiler.cs").Hash;launcher_source_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot\Launcher.c").Hash}|ConvertTo-Json|Set-Content "$outputTask\build.json"
Get-Content "$outputTask\build.json"
& "$PSScriptRoot\BuildClient.ps1"
