# Downloads the pinned LLVM-MinGW release into tools\ and checks its SHA-256 before unpacking.
# This is the only build dependency; nothing is installed system-wide.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest is far slower with the progress bar

$release = '20261104'
$name = "llvm-mingw-$release-ucrt-x86_64"
$sha256 = '552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f'
$url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$release/$name.zip"

$tools = Join-Path (Split-Path $PSScriptRoot) 'tools'
if (Test-Path (Join-Path $tools "$name\bin\x86_64-w64-mingw32-clang++.exe")) {
    Write-Host "$name is already in tools\"
    return
}
New-Item -ItemType Directory -Force $tools | Out-Null
$zip = Join-Path $tools "$name.zip"

Write-Host "Downloading $url (~190 MB)"
Invoke-WebRequest $url -OutFile $zip -UseBasicParsing
$actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $sha256) {
    Remove-Item $zip
    throw "Checksum mismatch for $name.zip: expected $sha256, got $actual"
}

Write-Host "Checksum OK, unpacking"
Expand-Archive $zip -DestinationPath $tools -Force
Remove-Item $zip
Write-Host "Toolchain ready: tools\$name"
