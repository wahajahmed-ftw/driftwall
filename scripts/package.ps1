# Builds a release zip in dist\: both programs, the built-in wallpapers (as a ready-to-use Library\),
# the install/uninstall scripts with double-click .cmd launchers, and the licenses.
param([switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $NoBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }

$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
$name = "Driftwall-$version-win-x64"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
Remove-Item $stage, "$stage.zip" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item "$root\build\DriftwallEngine.exe", "$root\build\Driftwall.exe" $stage
Copy-Item "$root\wallpapers" (Join-Path $stage 'Library') -Recurse
Copy-Item "$root\scripts\install.ps1", "$root\scripts\uninstall.ps1" $stage
Copy-Item "$root\LICENSE", "$root\THIRD-PARTY-NOTICES.md" $stage
foreach ($script in 'install', 'uninstall') {
    "@powershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0$script.ps1`" %*`r`n@pause" |
        Set-Content (Join-Path $stage "$script.cmd") -Encoding ASCII -NoNewline
}
@"
Driftwall $version - lightweight live wallpapers for Windows 10 and 11

Install:   double-click install.cmd   (installs to %LOCALAPPDATA%\Programs\Driftwall and starts with Windows)
Portable:  or just run Driftwall.exe from this folder
Uninstall: double-click uninstall.cmd (your wallpaper library is kept)

More: https://github.com/wahajahmed-ftw/driftwall
"@ | Set-Content (Join-Path $stage 'README.txt') -Encoding UTF8

Compress-Archive -Path $stage -DestinationPath "$stage.zip"
$hash = (Get-FileHash "$stage.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content "$stage.zip.sha256" -Encoding ASCII
Write-Host "Packaged dist\$name.zip ($([int]((Get-Item "$stage.zip").Length / 1KB)) KB)  sha256 $hash"
