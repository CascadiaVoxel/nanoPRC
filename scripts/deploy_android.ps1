$AdbPath = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
# Updated: Points to your x86_64 emulator build and targets the generated .so library file
$BinarySource = "$PSScriptRoot/../out/build/android-emulator/lib/libmobile_viewer.so"
$BinaryTarget = "/data/local/tmp/libmobile_viewer.so"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "   nanoPRC Android Automation Pipeline   " -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

if (-not (Test-Path $AdbPath)) { Write-Error "Could not find adb.exe"; exit 1 }
if (-not (Test-Path $BinarySource)) { Write-Error "Could not find compiled libmobile_viewer.so"; exit 1 }

# 1. Upload the native activity library payload straight to temporary storage
Write-Host "[1/2] Syncing library payload with emulator sandbox..." -ForegroundColor Yellow
& $AdbPath push $BinarySource $BinaryTarget

if ($LASTEXITCODE -ne 0) {
    Write-Error "File upload failed. Is your Android emulator currently running?"
    exit 1
}

# 2. Force the Android Activity Manager to boot your C++ main loop inside a native container window
Write-Host "[2/2] Launching Native Activity wrapper context on emulator screen..." -ForegroundColor Green
Write-Host "-----------------------------------------" -ForegroundColor Gray
& $AdbPath shell am start -n com.android.native_activity/.NativeActivity -e lib $BinaryTarget
