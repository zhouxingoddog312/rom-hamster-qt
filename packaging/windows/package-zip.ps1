# 组装 Windows zip（§9.1）：cmake --install 到暂存目录，再压缩。
# 用法（PowerShell，仓库根目录）：
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
    throw "找不到构建目录 $build，先跑 cmake --preset windows-cli"
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null

Write-Host "==> cmake --install $build"
cmake --install $build --prefix $stage --config Release

Copy-Item (Join-Path $root "LICENSE") $stage -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "README.md") $stage -ErrorAction SilentlyContinue

if ($Gui) {
    $gui = Join-Path $root "build/windows-gui"
    if (-not (Test-Path $gui)) { throw "找不到 build/windows-gui，先构建 windows-gui" }
    cmake --install $gui --prefix $stage --config Release
    $exe = Join-Path $stage "bin/rom-hamster-gui.exe"
    $windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($windeployqt) {
        Write-Host "==> windeployqt"
        & $windeployqt.Source --release --no-translations --no-opengl-sw $exe
    } else {
        Write-Warning "未找到 windeployqt，zip 内 GUI 依赖系统 Qt，仅供开发验证"
    }
}

$ver = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$suffix = if ($Gui) { "windows-x64" } else { "windows-x64-cli" }
$zip = Join-Path $dist "rom-hamster-$ver-$suffix.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "==> 产物：$zip"
