# Removes Driftwall: stops it, removes the startup entry and Start menu shortcut, and deletes the program files.
# Your wallpaper library is kept unless you pass -RemoveLibrary.
param([string]$InstallDir, [switch]$RemoveLibrary)
$ErrorActionPreference = 'Stop'

$lnk = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Driftwall.lnk'
if (-not $InstallDir) {
    # Find the install folder from the shortcut the installer created.
    if (Test-Path $lnk) { $InstallDir = Split-Path (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath }
    else { $InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\Driftwall' }
}
if (-not (Test-Path (Join-Path $InstallDir 'DriftwallEngine.exe'))) { throw "Driftwall was not found in $InstallDir" }

# Stopping the engine also gives each monitor its normal Windows wallpaper back.
Start-Process (Join-Path $InstallDir 'DriftwallEngine.exe') -ArgumentList '--exit' -Wait
Get-Process Driftwall -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 50 -and (Get-Process DriftwallEngine -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 100 }

Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'Driftwall' -ErrorAction SilentlyContinue
Remove-Item $lnk -ErrorAction SilentlyContinue

if ($RemoveLibrary) {
    Remove-Item $InstallDir -Recurse -Force
    Write-Host "Removed $InstallDir"
} else {
    Get-ChildItem $InstallDir -Exclude 'Library' | Remove-Item -Recurse -Force
    Write-Host "Driftwall removed. Your library is still in $(Join-Path $InstallDir 'Library')"
}
