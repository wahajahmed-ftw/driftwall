# Builds build\DriftwallEngine.exe (C++, LLVM-MinGW in tools\) and build\Driftwall.exe (C#, Windows' built-in csc).
# Run scripts\get-toolchain.ps1 once first to download LLVM-MinGW.
param([switch]$EngineOnly, [switch]$GalleryOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force $build | Out-Null

$version = (Get-Content (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw "VERSION must look like 1.2.3 (got '$version')" }
$major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]
Write-Host "Driftwall $version"

if (-not $GalleryOnly) {
    $toolchain = Get-ChildItem (Join-Path $root 'tools') -Directory -Filter 'llvm-mingw*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    $bin = if ($toolchain) { Join-Path $toolchain.FullName 'bin' } else { $null }
    if (-not $bin -or -not (Test-Path "$bin\x86_64-w64-mingw32-clang++.exe")) {
        throw "LLVM-MinGW not found in tools\. Run scripts\get-toolchain.ps1 first."
    }
    $defines = "-DDW_VER_MAJOR=$major", "-DDW_VER_MINOR=$minor", "-DDW_VER_PATCH=$patch"
    & "$bin\x86_64-w64-mingw32-windres.exe" $defines "$root\engine\engine.rc" -O coff -o "$build\engine.res.o"
    if ($LASTEXITCODE) { throw "windres failed" }
    $src = Get-ChildItem "$root\engine\*.cpp" | ForEach-Object { $_.FullName }
    & "$bin\x86_64-w64-mingw32-clang++.exe" -std=c++17 -O2 -municode -mwindows -static -s `
        -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers `
        -Wno-unknown-pragmas $src "$build\engine.res.o" -o "$build\DriftwallEngine.exe" `
        -ld3d11 -ldxgi -lmfplat -lole32 -loleaut32 -luuid -ldwmapi -lwtsapi32 -lshcore -lwindowscodecs `
        -lgdi32 -luser32 -lshell32 -ladvapi32 -lpropsys
    if ($LASTEXITCODE) { throw "engine build failed" }
    Write-Host "Built $build\DriftwallEngine.exe ($([int]((Get-Item "$build\DriftwallEngine.exe").Length / 1KB)) KB)"
}

if (-not $EngineOnly) {
    $csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
    if (-not (Test-Path $csc)) { throw ".NET Framework 4.x compiler not found at $csc" }
    $info = Join-Path $build 'AssemblyInfo.g.cs'
    @"
using System.Reflection;
[assembly: AssemblyTitle("Driftwall")]
[assembly: AssemblyProduct("Driftwall")]
[assembly: AssemblyDescription("Driftwall live wallpaper gallery")]
[assembly: AssemblyCopyright("MIT License")]
[assembly: AssemblyVersion("$version.0")]
[assembly: AssemblyFileVersion("$version.0")]
[assembly: AssemblyInformationalVersion("$version")]
"@ | Set-Content $info -Encoding UTF8
    $src = @(Get-ChildItem "$root\gallery\*.cs" | ForEach-Object { $_.FullName }) + $info
    & $csc /nologo /codepage:65001 /target:winexe /optimize+ /platform:x64 /warn:4 `
        /win32icon:"$root\assets\Driftwall.ico" /resource:"$root\assets\logo.png,Driftwall.logo.png" `
        /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll `
        /out:"$build\Driftwall.exe" $src
    if ($LASTEXITCODE) { throw "gallery build failed" }
    Write-Host "Built $build\Driftwall.exe ($([int]((Get-Item "$build\Driftwall.exe").Length / 1KB)) KB)"
}
