# Assemble the Windows zip (ARCHITECTURE.md 9.1): cmake --install into a staging
# directory, then compress.
#
# The whole file is deliberately ASCII-only: Windows PowerShell 5.1 reads
# scripts as ANSI when there is no BOM, so any non-ASCII byte can break
# parsing on machines with a legacy code page.
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1 -Gui
param(
    [switch]$Gui,
    [string]$BuildDir = "build/windows-cli",
    [string]$StageDir = "build/stage-windows"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$build = Join-Path $root $BuildDir
$stage = Join-Path $root $StageDir
$dist = Join-Path $root "dist"

if (-not (Test-Path $build)) {
    throw "Build directory not found: $build (run: cmake --preset windows-cli)"
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null

Write-Host "[1/4] cmake --install $build"
cmake --install $build --prefix $stage --config Release

Copy-Item (Join-Path $root "LICENSE") $stage -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "README.md") $stage -ErrorAction SilentlyContinue

if ($Gui) {
    $gui = Join-Path $root "build/windows-gui"
    if (-not (Test-Path $gui)) {
        throw "Build directory not found: build/windows-gui (build windows-gui first)"
    }
    Write-Host "[2/4] cmake --install $gui"
    cmake --install $gui --prefix $stage --config Release
    $exe = Join-Path $stage "bin/rom-hamster-gui.exe"
    $windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($windeployqt) {
        Write-Host "[3/4] windeployqt"
        & $windeployqt.Source --release --no-translations --no-opengl-sw $exe
    } else {
        Write-Warning "windeployqt not found; the GUI in this zip links against a system Qt and is for development only"
    }
} else {
    Write-Host "[2/4] CLI only (pass -Gui to include the GUI)"
}

$ver = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$suffix = if ($Gui) { "windows-x64" } else { "windows-x64-cli" }
$zip = Join-Path $dist "rom-hamster-$ver-$suffix.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Write-Host "[4/4] Compress-Archive -> $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "Done: $zip"
