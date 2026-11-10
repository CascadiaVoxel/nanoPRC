# Builds the Emscripten/WASM mobile_viewer target (see mobile_viewer/CMakeLists.txt's
# EMSCRIPTEN branch) and optionally serves it locally for testing in a browser.
#
# One-time setup this script assumes (not automated, since it's a machine-level
# toolchain install rather than part of the repo):
#   git clone https://github.com/emscripten-core/emsdk.git <EmsdkPath>
#   <EmsdkPath>/emsdk.bat install latest
#   <EmsdkPath>/emsdk.bat activate latest
#   pip install ninja   # the depot_tools "ninja" some machines have on PATH is a
#                        # non-functional shim that can't be used as a CMake generator
#
# Usage:
#   scripts/build_wasm.ps1                  # configure (if needed) + build (Release)
#   scripts/build_wasm.ps1 -Debug            # debug build: -O0, DWARF info, assertions
#   scripts/build_wasm.ps1 -Reconfigure      # force a fresh CMake configure
#   scripts/build_wasm.ps1 -Serve            # also start a local server afterward
#   scripts/build_wasm.ps1 -Debug -Serve -Port 9000
#
# -Debug uses a separate build-wasm-debug/ directory (not build-wasm/), so you
# can keep both a Release and a Debug build around without reconfiguring back
# and forth; see mobile_viewer/README.md for how to attach a debugger to it.

param(
    [switch]$Debug,
    [string]$EmsdkPath = $(if ($env:EMSDK) { $env:EMSDK } else { "$PSScriptRoot/../../emsdk" }),
    [string]$BuildDir = $(if ($Debug) { "$PSScriptRoot/../build-wasm-debug" } else { "$PSScriptRoot/../build-wasm" }),
    [switch]$Reconfigure,
    [switch]$Serve,
    [int]$Port = 8765
)

$RepoRoot = "$PSScriptRoot/.."
$BuildType = if ($Debug) { "Debug" } else { "Release" }

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "   nanoPRC WASM (Emscripten) Build - $BuildType" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

$EmsdkEnvScript = Join-Path $EmsdkPath "emsdk_env.ps1"
if (-not (Test-Path $EmsdkEnvScript)) {
    Write-Error "Could not find emsdk_env.ps1 under '$EmsdkPath'. Pass -EmsdkPath, or set `$env:EMSDK, to your emsdk checkout (see the one-time setup steps at the top of this script)."
    exit 1
}

Write-Host "[1/4] Activating Emscripten environment from $EmsdkPath..." -ForegroundColor Yellow
. $EmsdkEnvScript *>$null

# The depot_tools "ninja" some machines have earlier on PATH is just a shim that
# fails outside a Chromium checkout; prefer a real one from pip if present.
$RealNinja = python -c "import ninja, os; print(os.path.join(ninja.BIN_DIR, 'ninja.exe'))" 2>$null
if ($LASTEXITCODE -eq 0 -and (Test-Path $RealNinja)) {
    $NinjaDir = Split-Path $RealNinja
    $env:PATH = "$NinjaDir;$env:PATH"
}
$NinjaVersion = ninja --version 2>$null
if (-not $NinjaVersion) {
    Write-Error "No working 'ninja' found on PATH. Install one with: pip install ninja"
    exit 1
}
Write-Host "      Using ninja $NinjaVersion" -ForegroundColor Gray

$NeedsConfigure = $Reconfigure -or (-not (Test-Path (Join-Path $BuildDir "build.ninja")))
if ($NeedsConfigure) {
    Write-Host "[2/4] Configuring ($BuildDir, $BuildType)..." -ForegroundColor Yellow
    emcmake cmake -S $RepoRoot -B $BuildDir -G Ninja `
        "-DCMAKE_BUILD_TYPE=$BuildType" `
        -DNANOPRC_BUILD_MOBILE_VIEWER=ON `
        -DNANOPRC_BUILD_DEMOS=OFF `
        -DNANOPRC_BUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed."; exit 1 }
} else {
    Write-Host "[2/4] Build directory already configured, skipping (use -Reconfigure to force)." -ForegroundColor Gray
}

Write-Host "[3/4] Building mobile_viewer..." -ForegroundColor Yellow
cmake --build $BuildDir --target mobile_viewer
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit 1 }

$OutDir = Join-Path $BuildDir "bin"
Write-Host "Done. Output in $OutDir (mobile_viewer.html/.js/.wasm/.data)" -ForegroundColor Green

if ($Serve) {
    Write-Host "[4/4] Serving at http://localhost:$Port/mobile_viewer.html (Ctrl+C to stop)..." -ForegroundColor Yellow
    Push-Location $OutDir
    try {
        python -m http.server $Port
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[4/4] Skipped serving (pass -Serve to also start a local server)." -ForegroundColor Gray
}


Write-Host "[3/4] Building mobile_viewer..." -ForegroundColor Yellow
cmake --build $BuildDir --target mobile_viewer
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit 1 }

$OutDir = Join-Path $BuildDir "bin"
Write-Host "Done. Output in $OutDir (mobile_viewer.html/.js/.wasm/.data)" -ForegroundColor Green

if ($Serve) {
    Write-Host "[4/4] Serving at http://localhost:$Port/mobile_viewer.html (Ctrl+C to stop)..." -ForegroundColor Yellow
    Push-Location $OutDir
    try {
        python -m http.server $Port
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[4/4] Skipped serving (pass -Serve to also start a local server)." -ForegroundColor Gray
}
