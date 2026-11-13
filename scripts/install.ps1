# Installs Driftwall for the current user (no admin rights needed): copies the two programs and the built-in
# wallpapers, adds a Start menu shortcut and (unless -NoStartup) starts the engine with Windows.
# Re-running it upgrades in place; your library and config.json are kept.
#
# Works from the repository (after scripts\build.ps1) and from an unzipped release.
param([string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\Driftwall'), [switch]$NoStartup)
$ErrorActionPreference = 'Stop'

if (Test-Path (Join-Path $PSScriptRoot 'Driftwall.exe')) {
    # Release zip: everything sits next to this script.
    $bin = $PSScriptRoot
    $wallpapers = Join-Path $PSScriptRoot 'Library'
} else {
    $root = Split-Path $PSScriptRoot
    $bin = Join-Path $root 'build'
    $wallpapers = Join-Path $root 'wallpapers'
}
foreach ($exe in 'DriftwallEngine.exe', 'Driftwall.exe') {
    if (-not (Test-Path (Join-Path $bin $exe))) { throw "$exe not found in $bin. Build first with scripts\build.ps1." }
}

$library = Join-Path $InstallDir 'Library'
New-Item -ItemType Directory -Force $library | Out-Null

# Stop the running engine and gallery so their files can be replaced.
$engine = Join-Path $InstallDir 'DriftwallEngine.exe'
if (Test-Path $engine) { Start-Process $engine -ArgumentList '--exit' -Wait }
Get-Process Driftwall -ErrorAction SilentlyContinue | Stop-Process -Force
for ($i = 0; $i -lt 50 -and (Get-Process DriftwallEngine -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 100 }

Copy-Item (Join-Path $bin 'DriftwallEngine.exe'), (Join-Path $bin 'Driftwall.exe') $InstallDir -Force
Get-ChildItem $InstallDir -Filter '*.exe' | Unblock-File  # drop the "downloaded from the internet" mark

# Built-in wallpapers are added once; a copy the user already has (possibly edited) is left alone.
if (Test-Path $wallpapers) {
    foreach ($wp in Get-ChildItem $wallpapers -Directory) {
        $target = Join-Path $library $wp.Name
        if (-not (Test-Path $target)) { Copy-Item $wp.FullName $target -Recurse }
    }
}

$lnk = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Driftwall.lnk'
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$shortcut.TargetPath = Join-Path $InstallDir 'Driftwall.exe'
$shortcut.WorkingDirectory = $InstallDir
$shortcut.IconLocation = "$(Join-Path $InstallDir 'Driftwall.exe'),0"
$shortcut.Description = 'Driftwall live wallpapers'
$shortcut.Save()

$run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ($NoStartup) { Remove-ItemProperty $run -Name 'Driftwall' -ErrorAction SilentlyContinue }
else { Set-ItemProperty $run -Name 'Driftwall' -Value "`"$engine`"" }

Start-Process $engine -WorkingDirectory $InstallDir
Write-Host "Driftwall installed to $InstallDir. Open it from the Start menu to choose your wallpapers."
