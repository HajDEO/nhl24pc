# Configures and builds nhlport (the shadPS4 fork in ..\nhlport) the way shadPS4's Windows CI does:
# CMake + Ninja + clang-cl inside a Visual Studio x64 developer environment.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_nhlport.ps1            # Release
#   powershell ... -File tools\build_nhlport.ps1 -Config RelWithDebInfo -Clean
param(
    [string]$Source = (Join-Path $PSScriptRoot '..'),
    [string]$Build = (Join-Path $PSScriptRoot '..\build'),
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$NoLto,
    [int]$Jobs = 0,
    # Optional Ninja targets (e.g. object files) to build instead of everything; lets the sources
    # compile while a previous shadPS4.exe is still running and cannot be relinked.
    [string[]]$Targets = @(),
    # Add CodeView debug info (/Z7 + /DEBUG) to the Release build so crashes and freezes can be
    # symbolized with lldb / llvm-symbolizer; clang's codegen does not change, only a PDB appears.
    [switch]$DebugInfo
)
$ErrorActionPreference = 'Stop'

function Find-Tool([string]$name, [string[]]$candidates) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw "$name not found; run the toolchain installation first (see README.md)"
}

# Visual Studio (MSVC headers/libs + Windows SDK) via vswhere; load its x64 environment.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: Visual Studio Build Tools are not installed" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "no Visual Studio installation with the C++ toolset (VC.Tools.x86.x64) found" }
Write-Host "Visual Studio: $vsPath"
$devShell = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

# winget installs CMake and Ninja as portable packages; the Links shims and PATH entries only
# appear in new login sessions, so look in the package folders directly.
$wingetPkgs = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$cmakeCandidates = @("$env:ProgramFiles\CMake\bin\cmake.exe") + @(Get-ChildItem -Path $wingetPkgs -Recurse -Filter 'cmake.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$ninjaCandidates = @("$env:LOCALAPPDATA\Microsoft\WinGet\Links\ninja.exe") + @(Get-ChildItem -Path $wingetPkgs -Recurse -Filter 'ninja.exe' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$cmake = Find-Tool 'cmake' $cmakeCandidates
$ninja = Find-Tool 'ninja' $ninjaCandidates
$clang = Find-Tool 'clang-cl' @("$env:ProgramFiles\LLVM\bin\clang-cl.exe")
# Prefer the real installation over the Microsoft Store alias in WindowsApps.
$pythonCandidates = @("$env:LOCALAPPDATA\Programs\Python\Python312\python.exe", "$env:ProgramFiles\Python312\python.exe")
$python = $pythonCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $python) { $python = Find-Tool 'python' @() }
# Make them visible to CMake's sub-processes too (glslang's generators need python).
$env:PATH = (Split-Path $python) + ';' + (Split-Path $clang) + ';' + (Split-Path $cmake) + ';' + (Split-Path $ninja) + ';' + $env:PATH
Write-Host "cmake: $cmake`nninja: $ninja`nclang-cl: $clang`npython: $python"
& $clang --version | Select-Object -First 1

if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }
if ($Jobs -le 0) { $Jobs = $env:NUMBER_OF_PROCESSORS }
$lto = if ($NoLto) { 'OFF' } else { 'ON' }
$extra = @()
if ($DebugInfo) {
    $extra += '-DCMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7'
    $extra += '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7'
    $extra += '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /DEBUG'
}

& $cmake -S $Source -B $Build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=$lto" `
    "-DCMAKE_C_COMPILER=$clang" `
    "-DCMAKE_CXX_COMPILER=$clang" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DENABLE_DISCORD_RPC=OFF `
    -DENABLE_UPDATER=OFF `
    -DHAVE_ENDIAN_H=0 `
    @extra
# HAVE_ENDIAN_H=0: LLVM 23 ships lib/clang/23/include/endian.h, which makes libressl's
# check_include_files(endian.h) succeed on Windows and skip its winsock-based byte-order shim
# (undeclared htobe32/be64toh errors). Forcing the result restores the Windows code path.
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

# -File passes a comma-separated list as one string: split it.
$Targets = @($Targets | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($Targets.Count -gt 0) {
    & $cmake --build $Build --config $Config --parallel $Jobs --target @Targets
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    return
}
& $cmake --build $Build --config $Config --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Get-Item (Join-Path $Build 'nhl24.exe') | Select-Object FullName, Length, LastWriteTime

# Explorer caches exe icons per path; refresh it so the rebuilt nhl24.exe shows its current icon
try { Start-Process -FilePath (Join-Path $env:SystemRoot "System32\ie4uinit.exe") -ArgumentList "-show" -WindowStyle Hidden -ErrorAction Stop } catch {}
