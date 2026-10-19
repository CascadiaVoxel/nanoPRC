#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADB_PATH="$HOME/Library/Android/sdk/platform-tools/adb"
# Updated: Added parent path locator (..) to look outside the scripts folder
BINARY_SOURCE="$SCRIPT_DIR/../out/build/android-arm64/lib/libmobile_viewer.so"
BINARY_TARGET="/data/local/tmp/libmobile_viewer.so"

echo "========================================="
echo "   nanoPRC Android Automation Pipeline   "
echo "========================================="

if [ ! -f "$BINARY_SOURCE" ]; then
    echo "Error: Compiled binary target not found at $BINARY_SOURCE"
    exit 1
}

echo "[1/3] Uploading binary payload to virtual device..."
$ADB_PATH push $BINARY_SOURCE $BINARY_TARGET

echo "[2/3] Setting execution flags inside kernel sandboxes..."
$ADB_PATH shell chmod 755 $BINARY_TARGET

echo "[3/3] Launching mobile renderer loop! Press Ctrl+C to close..."
echo "-----------------------------------------"
$ADB_PATH shell $BINARY_TARGET