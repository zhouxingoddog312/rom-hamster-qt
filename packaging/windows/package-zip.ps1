# Assemble the Windows zip (ARCHITECTURE.md 9.1): cmake --install into a staging
# directory, then compress.
#
# Tool location: on Windows cmake/ninja/windeployqt usually live in an MSYS2
# install and are NOT on the system PATH. This script resolves each tool itself,
# in this order:
#   1. an explicit parameter (-CMake, -Ninja, -WindeployQt)
#   2. an environment variable (RH_CMAKE/CMAKE_BIN/CMAKE, RH_NINJA/NINJA_BIN,
#      RH_WINDEPLOYQT/WINDEPLOYQT, or QTDIR for windeployqt)
#   3. a known MSYS2 layout: <root>\{mingw64,ucrt64,clang64,usr}\bin, where
#      <root> is %MSYS2_ROOT%, C:\msys64 or %USERPROFILE%\msys64
#   4. PATH
# If nothing matches, it stops with an error that names the missing executable,
# lists every path it tried, and shows how to point the script at it.
#
# The zip must be self-contained: a MinGW build links against libglib,
# libsqlite3, libxml2, libarchive, ... which normally live in MSYS2 and are not
# on a stock Windows 11 PATH. After installing, the script walks the PE import
# table with objdump and copies every dependency it can find in the toolchain
# into bin\, then verifies the result with a System32-only PATH.
#
# The whole file is deliberately ASCII-only: Windows PowerShell 5.1 reads
# scripts as ANSI when there is no BOM, so any non-ASCII byte can break
# parsing on machines with a legacy code page.
#
# Usage (from the repository root):
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1 -Gui
#   powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1 `
#       -CMake "D:\msys64\mingw64\bin\cmake.exe"
param(
    [switch]$Gui,
    [string]$BuildDir = $(if ($Gui) { "build/windows-gui" } else { "build/windows-cli" }),
    [string]$StageDir = "build/stage-windows",
    [string]$CMake,
    [string]$Ninja,
    [string]$WindeployQt,
    [string]$Objdump
)

$ErrorActionPreference = "Stop"

$msysSubdirs = @("mingw64\bin", "ucrt64\bin", "clang64\bin", "usr\bin")

function Get-Msys2Roots {
    $roots = New-Object System.Collections.Generic.List[string]
    if ($env:MSYS2_ROOT) { $roots.Add($env:MSYS2_ROOT) }
    $roots.Add("C:\msys64")
    if ($env:USERPROFILE) { $roots.Add((Join-Path $env:USERPROFILE "msys64")) }
    return $roots
}

# Every path this script is willing to look at, in probe order.
function Get-ToolCandidates {
    param([string]$FileName)
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($root in (Get-Msys2Roots)) {
        foreach ($sub in $msysSubdirs) {
            $candidates.Add((Join-Path $root (Join-Path $sub $FileName)))
        }
    }
    return $candidates
}

function Resolve-Tool {
    param(
        [string]$Display,
        [string]$FileName,
        [string]$ParamName,
        [string]$Explicit,
        [string[]]$EnvNames = @(),
        [string[]]$ExtraCandidates = @(),
        [string]$PackageHint
    )

    # 1. explicit parameter: accept the executable or its directory
    if ($Explicit) {
        $path = $Explicit
        if (Test-Path -LiteralPath $path -PathType Container) {
            $path = Join-Path $path $FileName
        }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
        throw ("-$ParamName '$Explicit' does not point to $FileName. Pass the full path to the executable, or a directory that contains it.")
    }

    # 2. environment variables: accept the executable or its directory
    $envNotes = New-Object System.Collections.Generic.List[string]
    foreach ($name in $EnvNames) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not $value) { continue }
        $path = $value
        if (Test-Path -LiteralPath $path -PathType Container) {
            $path = Join-Path $path $FileName
        }
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
        $envNotes.Add("`$env:$name = '$value' (no $FileName at that location)")
    }

    # 3. MSYS2 layouts and caller supplied candidates
    $candidates = @(Get-ToolCandidates -FileName $FileName)
    foreach ($extra in $ExtraCandidates) {
        if ($extra) { $candidates += $extra }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    # 4. PATH
    $command = Get-Command $FileName -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command -and $command.Source) {
        return $command.Source
    }

    # 5. fail with something the user can act on
    $msg = New-Object System.Collections.Generic.List[string]
    $msg.Add("$Display ($FileName) was not found.")
    $msg.Add("")
    $msg.Add("Looked for it in this order:")
    $msg.Add("  1. -$ParamName parameter : not given")
    if ($EnvNames.Count -gt 0) {
        $msg.Add("  2. environment variables : " + ($EnvNames -join ", ") + " (none usable)")
    } else {
        $msg.Add("  2. environment variables : none")
    }
    foreach ($note in $envNotes) {
        $msg.Add("       $note")
    }
    $msg.Add("  3. MSYS2 install locations :")
    foreach ($candidate in $candidates) {
        $msg.Add("       $candidate")
    }
    $msg.Add("  4. PATH : no $FileName")
    $msg.Add("")
    $msg.Add("How to fix it (pick one):")
    $msg.Add("  a) Point the script at an existing copy:")
    $msg.Add("       powershell -ExecutionPolicy Bypass -File packaging\windows\package-zip.ps1 -$ParamName `"D:\msys64\mingw64\bin\$FileName`"")
    if ($EnvNames.Count -gt 0) {
        $msg.Add("     or set an environment variable for this session, then rerun:")
        $msg.Add("       `$env:$($EnvNames[0]) = `"D:\msys64\mingw64\bin\$FileName`"")
    }
    $msg.Add("  b) Install MSYS2 (https://www.msys2.org) and add ${Display}:")
    $msg.Add("       pacman -S $PackageHint")
    $msg.Add("     The script looks for <MSYS2>\mingw64\bin\$FileName by default.")
    $msg.Add("  c) Or add the tool directory to PATH for this session:")
    $msg.Add("       `$env:PATH = `"D:\msys64\mingw64\bin;`$env:PATH`"")
    throw ($msg -join [Environment]::NewLine)
}

# Imported DLL names of one PE file, as reported by objdump -p.
function Get-PeImports {
    param([string]$Path, [string]$Objdump)
    $lines = & $Objdump -p $Path 2>$null
    foreach ($line in $lines) {
        if ($line -match '^\s*DLL Name:\s*(\S+)\s*$') {
            $matches[1]
        }
    }
}

# Copy the transitive non-system dependencies of $Roots next to them.
# A dependency that is not found in $SearchDirs is assumed to be a Windows
# system DLL (KERNEL32, msvcrt, ...) and is left alone; anything found in the
# toolchain is copied, because a stock Windows 11 has none of it.
function Copy-RuntimeDlls {
    param(
        [string]$StageBin,
        [string]$Objdump,
        [string[]]$Roots,
        [string[]]$SearchDirs
    )
    $queue = New-Object System.Collections.Queue
    $seen = @{}
    foreach ($rootFile in $Roots) {
        $queue.Enqueue($rootFile)
    }
    $copied = New-Object System.Collections.Generic.List[string]

    while ($queue.Count -gt 0) {
        $pe = [string]$queue.Dequeue()
        foreach ($name in (Get-PeImports -Path $pe -Objdump $Objdump)) {
            $key = $name.ToLowerInvariant()
            if ($seen.ContainsKey($key)) { continue }
            $seen[$key] = $true

            $dest = Join-Path $StageBin $name
            if (Test-Path -LiteralPath $dest) {
                # Already shipped (our own libromham.dll, or a DLL that
                # windeployqt copied): still scan it for its own dependencies.
                $queue.Enqueue($dest)
                continue
            }

            $source = $null
            foreach ($dir in $SearchDirs) {
                $candidate = Join-Path $dir $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $source = (Resolve-Path -LiteralPath $candidate).Path
                    break
                }
            }
            if (-not $source) { continue }

            Copy-Item -LiteralPath $source -Destination $dest
            $copied.Add($name)
            $queue.Enqueue($dest)
        }
    }

    return $copied
}

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$build = Join-Path $root $BuildDir
$stage = Join-Path $root $StageDir
$dist = Join-Path $root "dist"
$preset = if ($Gui) { "windows-gui" } else { "windows-cli" }
$defaultBuildDir = if ($Gui) { "build/windows-gui" } else { "build/windows-cli" }

Write-Host "[1/5] resolve tools"
$cmakeExe = Resolve-Tool -Display "CMake" -FileName "cmake.exe" `
    -ParamName "CMake" -Explicit $CMake `
    -EnvNames @("RH_CMAKE", "CMAKE_BIN", "CMAKE") `
    -PackageHint "mingw-w64-x86_64-cmake"
Write-Host "      cmake: $cmakeExe"

# objdump walks the PE import table; it ships with binutils, next to gcc.
$objdumpExe = Resolve-Tool -Display "objdump" -FileName "objdump.exe" `
    -ParamName "Objdump" -Explicit $Objdump `
    -EnvNames @("RH_OBJDUMP") `
    -PackageHint "mingw-w64-x86_64-binutils"
Write-Host "      objdump: $objdumpExe"

# Tools spawned by cmake (ninja, compiler, pkg-config) must be findable too.
$cmakeDir = Split-Path -Parent $cmakeExe
if (($env:PATH -split ';') -notcontains $cmakeDir) {
    $env:PATH = "$cmakeDir;$env:PATH"
}

Write-Host "[2/5] cmake --install $build"
if (-not (Test-Path -LiteralPath (Join-Path $build "CMakeCache.txt"))) {
    # Not configured yet: configure it here with the preset, so the caller does
    # not have to run cmake manually in a shell where it is not on PATH.
    $ninjaExe = Resolve-Tool -Display "Ninja" -FileName "ninja.exe" `
        -ParamName "Ninja" -Explicit $Ninja `
        -EnvNames @("RH_NINJA", "NINJA_BIN") `
        -PackageHint "mingw-w64-x86_64-ninja"
    Write-Host "      ninja: $ninjaExe"
    if ($BuildDir -ne $defaultBuildDir) {
        throw ((@(
            "Build directory is not configured: $build",
            "It does not match preset '$preset' (expected $defaultBuildDir), so it cannot be configured automatically.",
            "Configure it manually, then rerun this script:",
            "  & `"$cmakeExe`" -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninjaExe`" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRH_GUI=$(if ($Gui) { 'ON' } else { 'OFF' })",
            "  & `"$cmakeExe`" --build `"$build`""
        )) -join [Environment]::NewLine)
    }
    Write-Host "      not configured yet, running: cmake --preset $preset"
    Push-Location $root
    try {
        & $cmakeExe --preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "cmake --preset $preset failed (exit code $LASTEXITCODE)."
        }
    } finally {
        Pop-Location
    }
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage, $dist | Out-Null

& $cmakeExe --install $build --prefix $stage --config Release
if ($LASTEXITCODE -ne 0) {
    throw "cmake --install failed (exit code $LASTEXITCODE)."
}

Copy-Item (Join-Path $root "LICENSE") $stage -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root "README.md") $stage -ErrorAction SilentlyContinue

if ($Gui) {
    $windeployqtCandidates = @()
    if ($env:QTDIR) {
        $windeployqtCandidates += (Join-Path $env:QTDIR "bin\windeployqt.exe")
    }
    $windeployqtExe = Resolve-Tool -Display "windeployqt" -FileName "windeployqt.exe" `
        -ParamName "WindeployQt" -Explicit $WindeployQt `
        -EnvNames @("RH_WINDEPLOYQT", "WINDEPLOYQT") `
        -ExtraCandidates $windeployqtCandidates `
        -PackageHint "mingw-w64-x86_64-qt6-base"

    $guiExe = Join-Path $stage "bin/rom-hamster-gui.exe"
    if (-not (Test-Path -LiteralPath $guiExe)) {
        throw "$guiExe is missing; the GUI build directory does not contain a built rom-hamster-gui."
    }
    Write-Host "[3/5] windeployqt: $windeployqtExe"
    & $windeployqtExe --release --no-translations --no-opengl-sw $guiExe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed (exit code $LASTEXITCODE)."
    }
} else {
    Write-Host "[3/5] CLI only (pass -Gui to include the GUI)"
}

# The MinGW runtime (glib, sqlite3, libxml2, libarchive, zlib, ...) lives in
# MSYS2 and is on no stock Windows PATH. Copy the closure next to the binaries
# so the zip works on a machine that never had MSYS2.
Write-Host "[4/5] bundle runtime DLLs"
$stageBin = Join-Path $stage "bin"
$dllSearchDirs = @()
foreach ($msysRoot in (Get-Msys2Roots)) {
    foreach ($sub in $msysSubdirs) {
        $dllSearchDirs += (Join-Path $msysRoot $sub)
    }
}
$dllSearchDirs += $cmakeDir

$rootsToScan = @(Join-Path $stageBin "rom-hamster.exe")
if ($Gui) { $rootsToScan += (Join-Path $stageBin "rom-hamster-gui.exe") }
$bundled = Copy-RuntimeDlls -StageBin $stageBin -Objdump $objdumpExe `
    -Roots $rootsToScan -SearchDirs $dllSearchDirs
foreach ($name in $bundled) {
    Write-Host "      + $name"
}
Write-Host "      bundled $($bundled.Count) DLLs"

# Verify with a PATH that only has System32: this is what a clean Windows 11
# looks like, and it is the check that the old "run it from the repo" missed.
Write-Host "      verify: rom-hamster.exe --version with System32-only PATH"
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $null = & (Join-Path $stageBin "rom-hamster.exe") --version
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled CLI failed to start with a System32-only PATH (exit code $LASTEXITCODE). A runtime DLL is probably still missing."
    }
} finally {
    $env:PATH = $savedPath
}

# Project version: parse the project() call itself. A plain "VERSION x.y"
# search would match cmake_minimum_required(VERSION 3.22) first.
$cmakeLists = Get-Content -LiteralPath (Join-Path $root "CMakeLists.txt") -Raw
$versionMatch = [regex]::Match($cmakeLists,
    'project\s*\([^)]*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', 'Singleline')
if (-not $versionMatch.Success) {
    throw "Cannot read the project version from CMakeLists.txt"
}
$ver = $versionMatch.Groups[1].Value
$suffix = if ($Gui) { "windows-x64" } else { "windows-x64-cli" }
$zip = Join-Path $dist "rom-hamster-$ver-$suffix.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Write-Host "[5/5] Compress-Archive -> $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "Done: $zip"
