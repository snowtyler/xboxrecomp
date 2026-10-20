[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedSha256,[Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedLauncherSha256)
$ErrorActionPreference='Stop'
$identityTask=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not([Security.Principal.WindowsPrincipal]$identityTask).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run this installer once with Windows administrator consent'}
$sidTask=$identityTask.User.Value
$rootTask='C:\ProgramData\BruteForceProfiler'
$sourceTask=Join-Path $PSScriptRoot '..\..\games\brute_force_e3\analysis\profiler-helper-build\Profiler.exe'
$nsTask='C:\Program Files\NVIDIA Corporation\Nsight Systems 2026.5.1\target-windows-x64\nsys.exe'
if((Get-FileHash -LiteralPath $sourceTask -Algorithm SHA256).Hash -ne $ExpectedSha256){throw 'Prepared helper hash changed'}
$launcherSourceTask=Join-Path (Split-Path $sourceTask) 'Launcher.exe'
if((Get-FileHash -LiteralPath $launcherSourceTask).Hash -ne $ExpectedLauncherSha256){throw 'Prepared launcher hash changed'}
if(Test-Path -LiteralPath $rootTask){throw 'Install directory already exists; refusing to replace or trust it'}
if(Get-ScheduledTask -TaskName 'BruteForceProfiler-Collector' -ErrorAction SilentlyContinue){throw 'Task already exists'}
$signatureTask=Get-AuthenticodeSignature -LiteralPath $nsTask
if($signatureTask.Status -ne 'Valid' -or $signatureTask.SignerCertificate.Subject -notmatch 'O=NVIDIA Corporation'){throw 'Expected NVIDIA-signed Nsight executable not found'}
$aclTask=New-Object Security.AccessControl.DirectorySecurity
$aclTask.SetAccessRuleProtection($true,$false)
$adminsTask=New-Object Security.Principal.SecurityIdentifier('S-1-5-32-544')
if($identityTask.Owner.Value -ne $adminsTask.Value){throw 'Administrator default object owner required for protected files'}
$systemTask=New-Object Security.Principal.SecurityIdentifier('S-1-5-18')
$userTask=New-Object Security.Principal.SecurityIdentifier($sidTask)
$aclTask.SetOwner($adminsTask)
$inheritTask=[Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
foreach($principalTask in @($adminsTask,$systemTask)){
 $aclTask.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($principalTask,'FullControl',$inheritTask,'None','Allow')))
}
$aclTask.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($userTask,'ReadAndExecute',$inheritTask,'None','Allow')))
# Create with the protected ACL, rather than opening a user-writable interval.
$directoryTask=New-Object IO.DirectoryInfo($rootTask)
$directoryTask.Create($aclTask)
$createdAclTask=Get-Acl -LiteralPath $rootTask
if(($directoryTask.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $createdAclTask.GetOwner([Security.Principal.SecurityIdentifier]).Value -ne $adminsTask.Value -or $createdAclTask.GetSecurityDescriptorSddlForm('Access') -ne $aclTask.GetSecurityDescriptorSddlForm('Access')){throw 'Protected installation directory verification failed'}
Start-Transcript -LiteralPath (Join-Path $rootTask 'install.log')|Out-Null
foreach($childTask in @('recordings','temp','profile','profile\Local','profile\Roaming')){[void](New-Item -Path (Join-Path $rootTask $childTask) -ItemType Directory -Force)}
$installedTask=Join-Path $rootTask 'Profiler.exe'
Copy-Item -LiteralPath $sourceTask -Destination $installedTask
$launcherTask=Join-Path $rootTask 'Launcher.exe'
Copy-Item -LiteralPath $launcherSourceTask -Destination $launcherTask
if((Get-FileHash -LiteralPath $launcherTask).Hash -ne $ExpectedLauncherSha256){throw 'Installed launcher hash mismatch'}
if((Get-FileHash -LiteralPath $installedTask).Hash -ne $ExpectedSha256){throw 'Installed helper hash mismatch'}
@{sid=$sidTask;nsys=$nsTask;sha256=$ExpectedSha256;launcher_sha256=$ExpectedLauncherSha256;installed_utc=(Get-Date).ToUniversalTime().ToString('o');protocol=1}|ConvertTo-Json|Set-Content (Join-Path $rootTask 'config.json')
$actionTask=New-ScheduledTaskAction -Execute $launcherTask -WorkingDirectory $rootTask
$principalTask=New-ScheduledTaskPrincipal -UserId $sidTask -LogonType Interactive -RunLevel Highest
$settingsTask=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 36) -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName 'BruteForceProfiler-Collector' -Action $actionTask -Principal $principalTask -Settings $settingsTask -Description 'On-demand, fixed-operation WPR/Nsight collector; 30 minute session; four captures maximum.'|Out-Null
$serviceTask=New-Object -ComObject Schedule.Service
$serviceTask.Connect()
$taskTask=$serviceTask.GetFolder('\').GetTask('BruteForceProfiler-Collector')
$taskTask.SetSecurityDescriptor("O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGX;;;$sidTask)",0)
@{status='installed';root=$rootTask;sha256=(Get-FileHash -LiteralPath $installedTask).Hash;directory_sddl=(Get-Acl -LiteralPath $rootTask).Sddl;task_sddl=$taskTask.GetSecurityDescriptor(7);task_xml=$taskTask.Xml}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $rootTask 'installation.json')
Write-Output "Installed fixed profiling task for $($identityTask.Name)."
Stop-Transcript|Out-Null
