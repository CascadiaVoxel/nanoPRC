# mobile_viewer: WASM/Emscripten Build

`mobile_viewer` is an SDL3 + OpenGL ES3 demo viewer (see [src/main.cpp](src/main.cpp))
that also builds for the web via Emscripten, producing a self-contained
`mobile_viewer.html` + `.js` + `.wasm` + `.data` bundle you can open in a
browser. (It also has Android/iOS/desktop CMake branches in
[CMakeLists.txt](CMakeLists.txt); this document only covers the WASM build.)

## One-time setup

1. **Emscripten SDK** (not part of this repo -- install it as a sibling
   checkout next to this one, e.g. `F:\3d\emsdk` alongside `F:\3d\nanoPRC_public`):
   ```powershell
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   .\emsdk.bat install latest
   .\emsdk.bat activate latest
   ```
   If you install it somewhere else, pass `-EmsdkPath <path>` to the build
   script below, or set `$env:EMSDK` to that path.

2. **A real `ninja`.** Emscripten's CMake toolchain only works with the
   Ninja or NMake Makefiles generators (not the Visual Studio generator
   CMake defaults to on Windows), and depot_tools' `ninja` shim (if you have
   one on PATH from another project) isn't a real binary:
   ```powershell
   pip install ninja
   ```

That's it -- no changes needed to `nano_prc`, SDL3, or zlib; all three build
from source against the Emscripten toolchain unmodified via the normal
`add_subdirectory(thirdparty)` path.

## Build

```powershell
.\scripts\build_wasm.ps1
```

This activates the Emscripten environment, configures `build-wasm/` with
`emcmake cmake -G Ninja` (only on first run or after `-Reconfigure`), and
builds the `mobile_viewer` target. Output lands in
`build-wasm/bin/mobile_viewer.{html,js,wasm,data}`.

Useful flags:
```powershell
.\scripts\build_wasm.ps1 -Debug           # -O0, DWARF debug info, assertions, safe-heap (see Debugging below)
.\scripts\build_wasm.ps1 -Reconfigure     # force a fresh CMake configure
.\scripts\build_wasm.ps1 -Serve           # build, then also serve locally
.\scripts\build_wasm.ps1 -Serve -Port 9000
.\scripts\build_wasm.ps1 -EmsdkPath D:\tools\emsdk
```

`-Debug` builds into a separate `build-wasm-debug/` directory (not
`build-wasm/`), so you can keep a Release and a Debug build around at the
same time without reconfiguring back and forth every time you switch.

## Run

Browsers won't load WASM over `file://`, so serve the output directory over
plain HTTP and open it there. `-Serve` does this for you, or do it manually:

```powershell
cd build-wasm\bin
python -m http.server 8765
```

Then open `http://localhost:8765/mobile_viewer.html`.

## Debugging

Visual Studio can't debug this -- it's C/C++ compiled to WASM and executed
by the *browser's* own engine, not something the Windows native debugger
can attach to. Debugging instead happens through the browser's own devtools
reading DWARF debug info embedded in the `.wasm` file (that's what `-Debug`'s
`-g` link flag is for; see the `if(CMAKE_BUILD_TYPE STREQUAL "Debug")` block
in [CMakeLists.txt](CMakeLists.txt)). `-Debug` also adds `-sASSERTIONS=2` and
`-sSAFE_HEAP=1`, which turn a raw "memory access out of bounds" crash into a
message that actually points at the offending access -- these cost real
size/runtime overhead, which is why they're Debug-only, not in Release.

1. **Build a debug bundle and serve it:**
   ```powershell
   .\scripts\build_wasm.ps1 -Debug -Serve
   ```

2. **In the browser directly** (Chrome or Edge, both Chromium-based):
   install the **"C/C++ DevTools Support (DWARF)"** extension (Chrome Web
   Store / Edge Add-ons), open `http://localhost:8765/mobile_viewer.html`,
   open DevTools, and your actual `.cpp`/`.h` files show up in the Sources
   panel with working breakpoints/stepping/variables -- despite this being
   compiled code, not JavaScript.

3. **From VS Code instead:** install the **"WebAssembly DWARF Debugging"**
   extension (`ms-vscode.wasm-dwarf-debugging`) plus either the **"Microsoft
   Edge Tools for VS Code"** or Chrome debugger extension, then use
   `.vscode/launch.json`'s "WASM: Debug mobile_viewer in Edge" (or Chrome)
   configuration -- its `preLaunchTask` builds+serves the debug bundle via
   `.vscode/tasks.json`, then launches the browser attached, so you can set
   breakpoints directly in `main.cpp` from VS Code's own editor. Note
   `.vscode/` is git-ignored in this repo, so `launch.json`/`tasks.json`
   exist locally but aren't tracked -- recreate them (or `git add -f`) on a
   fresh checkout if you want this workflow available again.

## What it does right now

The WASM build loads [examples/cube.pdf](../examples/cube.pdf), baked into
the virtual filesystem at build time via `--preload-file`, and renders it
through the same SDL3/GLES3 path `main.cpp` uses on desktop/Android --
`ResolveCrossPlatformAssetPath`'s relative-path logic and
`prc_api_open_contents` are unmodified for the web. There's no "pick your
own file" UI yet; see the comment in the `EMSCRIPTEN` branch of
[CMakeLists.txt](CMakeLists.txt) for how that would hook in (writing a
picked `File`'s bytes into Emscripten's MEMFS from JS before calling the
same path-based API).

`mobile_viewer` overall (not just the WASM build) is still a work in
progress -- see [main.cpp](src/main.cpp) for the current state of the
renderer.
