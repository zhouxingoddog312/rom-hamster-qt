# Build the Windows installer from an already assembled GUI staging directory
# (ARCHITECTURE.md 9.1: rom-hamster-<ver>-win64-setup.exe).
#
# The staging directory is the same one package-zip.ps1 -Gui produces: bin\ with
# both executables, the MinGW runtime DLLs and the Qt deployment, plus etc\ and
# share\. Build it first if it does not exist:
#
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1 -Gui
#   powershell -ExecutionPolicy Bypass -File packaging/windows/make-setup.ps1
#
# ISCC.exe (Inno Setup command line compiler, 6.3 or newer) is resolved in this
# order:
#   1. -ISCC parameter
#   2. environment variables RH_ISCC / ISCC
#   3. a standard Inno Setup install location (Program Files, Program Files
#      (x86), %LOCALAPPDATA%\Programs; versions 6 and 7)
#   4. PATH
# If nothing matches, the error lists every path that was checked.
#
# ASCII only on purpose: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
param(
    [string]$StageDir = "build/stage-windows",
    [string]$ISCC,
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$iss = Join-Path $PSScriptRoot "rom-hamster.iss"

function Resolve-ISCC {
    param([string]$Explicit)

    if ($Explicit) {
        $path = $Explicit
        if (Test-Path -LiteralPath $path -PathType Container) {
            $path = Join-Path $path "ISCC.exe"
        }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
        throw "-ISCC '$Explicit' does not point to ISCC.exe."
    }

    $envNotes = New-Object System.Collections.Generic.List[string]
    foreach ($name in @("RH_ISCC", "ISCC")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not $value) { continue }
        $path = $value
        if (Test-Path -LiteralPath $path -PathType Container) {
            $path = Join-Path $path "ISCC.exe"
        }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
        $envNotes.Add("`$env:$name = '$value' (no ISCC.exe there)")
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    $bases = @()
    if (${env:ProgramFiles(x86)}) { $bases += ${env:ProgramFiles(x86)} }
    if ($env:ProgramFiles) { $bases += $env:ProgramFiles }
    if ($env:LOCALAPPDATA) { $bases += (Join-Path $env:LOCALAPPDATA "Programs") }
    foreach ($base in $bases) {
        foreach ($name in @("Inno Setup 7", "Inno Setup 6")) {
            $candidates.Add((Join-Path $base (Join-Path $name "ISCC.exe")))
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command "ISCC.exe" -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command -and $command.Source) {
        return $command.Source
    }

    $msg = New-Object System.Collections.Generic.List[string]
    $msg.Add("ISCC.exe (Inno Setup command line compiler) was not found.")
    $msg.Add("")
    $msg.Add("Looked in this order:")
    $msg.Add("  1. -ISCC parameter : not given")
    foreach ($note in $envNotes) { $msg.Add("  2. $note") }
    if ($envNotes.Count -eq 0) { $msg.Add("  2. environment variables : RH_ISCC, ISCC (neither set)") }
    $msg.Add("  3. standard install locations:")
    foreach ($candidate in $candidates) { $msg.Add("       $candidate") }
    $msg.Add("  4. PATH : no ISCC.exe")
    $msg.Add("")
    $msg.Add("How to fix it:")
    $msg.Add("  a) Install Inno Setup 6.3+ from https://jrsoftware.org/isdl.php")
    $msg.Add("  b) Or point the script at an existing compiler:")
    $msg.Add("       powershell -ExecutionPolicy Bypass -File packaging\windows\make-setup.ps1 -ISCC `"C:\path\to\ISCC.exe`"")
    throw ($msg -join [Environment]::NewLine)
}

# Project version: parse the project() call, not the first "VERSION" in the file
# (cmake_minimum_required(VERSION ...) comes first).
$cmakeLists = Get-Content -LiteralPath (Join-Path $root "CMakeLists.txt") -Raw
$versionMatch = [regex]::Match($cmakeLists,
    'project\s*\([^)]*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', 'Singleline')
if (-not $versionMatch.Success) {
    throw "Cannot read the project version from CMakeLists.txt"
}
$ver = $versionMatch.Groups[1].Value

$stage = [System.IO.Path]::GetFullPath((Join-Path $root $StageDir))
$stageBin = Join-Path $stage "bin"
foreach ($name in @("rom-hamster.exe", "rom-hamster-gui.exe")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageBin $name))) {
        throw ("$stageBin\$name is missing. The installer needs the GUI staging directory; build it with:`n" +
            "  powershell -ExecutionPolicy Bypass -File packaging\windows\package-zip.ps1 -Gui")
    }
}
$dllCount = @(Get-ChildItem -LiteralPath $stageBin -Filter "*.dll" -ErrorAction SilentlyContinue).Count
if ($dllCount -lt 10) {
    throw ("Only $dllCount DLLs next to the executables; the runtime bundle is missing. " +
        "Rebuild the staging directory with package-zip.ps1 -Gui.")
}

$isccExe = Resolve-ISCC -Explicit $ISCC
Write-Host "[1/2] ISCC: $isccExe"

$out = [System.IO.Path]::GetFullPath((Join-Path $root $OutDir))
New-Item -ItemType Directory -Force -Path $out | Out-Null

Write-Host "[2/2] compile $iss"
& $isccExe "/DMyAppVersion=$ver" "/DStageDir=$stage" "/DOutputDir=$out" $iss
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed (exit code $LASTEXITCODE)."
}

$setup = Join-Path $out "rom-hamster-$ver-win64-setup.exe"
if (-not (Test-Path -LiteralPath $setup)) {
    throw "ISCC reported success but $setup does not exist."
}

Write-Host ""
Write-Host "Done: $setup"
Write-Host ("  {0:N1} MB" -f ((Get-Item -LiteralPath $setup).Length / 1MB))
Write-Host "Manual checks still needed on a real machine (deferred):"
Write-Host "  - silent install: rom-hamster-$ver-win64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES"
Write-Host "  - both shortcuts start; rom-hamster.exe --version works from a new shell"
Write-Host "  - re-run the same setup: it replaces in place, no second entry in Apps"
Write-Host "  - uninstall: %APPDATA%\rom-hamster is kept"
