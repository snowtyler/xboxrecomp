$ErrorActionPreference='Stop'
$outputTask=Join-Path $PSScriptRoot '..\..\games\brute_force_e3\analysis\profiler-helper-build'
[void](New-Item -ItemType Directory -Path $outputTask -Force)
$compilerTask=Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compilerTask /nologo /target:exe /platform:x64 /optimize+ /reference:System.Web.Extensions.dll "/out:$outputTask\Client.exe" "$PSScriptRoot\Client.cs"
if($LASTEXITCODE -ne 0){throw 'Client compilation failed'}
& "$outputTask\Client.exe" selftest
if($LASTEXITCODE -ne 0){throw 'Client regression failed'}
@{client_sha256=(Get-FileHash -LiteralPath "$outputTask\Client.exe").Hash;source_sha256=(Get-FileHash -LiteralPath "$PSScriptRoot\Client.cs").Hash}|ConvertTo-Json|Set-Content "$outputTask\client-build.json"
