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
.\scripts\build_wasm.ps1 -Reconfigure     # force a fresh CMake configure
.\scripts\build_wasm.ps1 -Serve           # build, then also serve locally
.\scripts\build_wasm.ps1 -Serve -Port 9000
.\scripts\build_wasm.ps1 -EmsdkPath D:\tools\emsdk
```

## Run

Browsers won't load WASM over `file://`, so serve the output directory over
plain HTTP and open it there. `-Serve` does this for you, or do it manually:

```powershell
cd build-wasm\bin
python -m http.server 8765
```

Then open `http://localhost:8765/mobile_viewer.html`.

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
