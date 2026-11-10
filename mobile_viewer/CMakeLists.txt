cmake_minimum_required(VERSION 3.16)
project(mobile_viewer CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOBILE_VIEWER_SOURCES 
    "src/main.cpp"
)

# Branch target compilation rules dynamically based on configuration tags
if(ANDROID)
    add_library(mobile_viewer SHARED ${MOBILE_VIEWER_SOURCES})
    target_link_libraries(mobile_viewer PRIVATE 
        nano_prc SDL3-shared GLESv3 log android
    )
elseif(EMSCRIPTEN)
    # Web/WASM build: emits mobile_viewer.html + .js + .wasm. SDL3 targets
    # WebGL2 through the same GLES3 context request main.cpp already makes
    # for Android/desktop, so no renderer code changes are needed here --
    # only link-time flags.
    #
    # - ASYNCIFY: main.cpp's render loop is a plain blocking `while(running)`
    #   (the normal SDL desktop-app shape), not the callback-driven
    #   emscripten_set_main_loop style the web needs to let the browser
    #   paint/handle events between frames. Asyncify transforms the whole
    #   call graph so that blocking loop can still yield each iteration
    #   without rewriting main.cpp; costs some code size/perf, acceptable
    #   for this demo. If main.cpp is later restructured to call
    #   emscripten_set_main_loop directly, this can be dropped.
    # - prc_api_open_contents only takes a file path, no in-memory-buffer
    #   variant -- --preload-file packages examples/cube.pdf into the
    #   virtual filesystem (MEMFS) at /cube.pdf ahead of main() running, so
    #   ResolveCrossPlatformAssetPath's existing relative-path branch
    #   ("cube.pdf", resolved against MEMFS's root cwd) finds it unmodified.
    #   A real "pick your own file" page would instead write the picked
    #   File's bytes into MEMFS from JS at runtime before calling this same
    #   path-based API -- left as a follow-up once mobile_viewer's own
    #   asset handling is settled.
    add_executable(mobile_viewer ${MOBILE_VIEWER_SOURCES})
    # prc_static, not nano_prc: nano_prc is a SHARED library, and Emscripten
    # treats add_library(... SHARED ...) as a genuine WASM dynamic-linking
    # side module -- it has to be fetched over HTTP as its own .so file at
    # startup, separately from mobile_viewer.wasm, which just adds a
    # deployment footgun (two artifacts that must be served from the same
    # relative path) for no benefit in a single-page demo. prc_static is
    # the same source list built as an ordinary static library, so this
    # folds everything into one self-contained mobile_viewer.wasm instead.
    # SDL3-shared doesn't exist here -- SDL's own CMake only builds it when
    # SDL_SHARED is on, which it force-disables for Emscripten (static
    # only); SDL3::SDL3 is the alias SDL's CMakeLists guarantees always
    # resolves to whichever variant actually got built
    target_link_libraries(mobile_viewer PRIVATE prc_static SDL3::SDL3)
    set_target_properties(mobile_viewer PROPERTIES SUFFIX ".html")
    target_link_options(mobile_viewer PRIVATE
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY=1"
        "-sSTACK_SIZE=4194304"
        "-sEXPORTED_RUNTIME_METHODS=[\"FS\",\"ccall\",\"cwrap\"]"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=NanoPRCViewerModule"
        "--preload-file=${CMAKE_SOURCE_DIR}/examples/cube.pdf@/cube.pdf"
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
    # Debug-only diagnostics: -g keeps DWARF debug info in the wasm binary so
    # browser-side C/C++ source debugging works (see mobile_viewer/README.md
    # -- Chrome/Edge's "C/C++ DevTools Support (DWARF)" extension, or VS
    # Code's "WebAssembly DWARF Debugging" extension); -sASSERTIONS=2 and
    # -sSAFE_HEAP=1 turn a raw "memory access out of bounds" crash into an
    # actionable message pointing at the offending access. Left out of
    # Release builds: all three cost real size/runtime overhead, and
    # CMAKE_BUILD_TYPE=Debug (set by scripts/build_wasm.ps1 -Debug) already
    # gets emcc's own default -g/-O0 compile flags for the actual source
    # files on top of these
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(mobile_viewer PRIVATE
            "-g"
            "-sASSERTIONS=2"
            "-sSAFE_HEAP=1"
        )
    endif()
elseif(IOS)
    add_executable(mobile_viewer MACOSX_BUNDLE ${MOBILE_VIEWER_SOURCES})
    set_target_properties(mobile_viewer PROPERTIES
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    )
    target_link_libraries(mobile_viewer PRIVATE nano_prc SDL3-shared)
else()
    # This captures your local Windows testing profile seamlessly!
    add_executable(mobile_viewer ${MOBILE_VIEWER_SOURCES})
    
    # We remove all static library linking here because SDL3 handles function loading at runtime
    target_link_libraries(mobile_viewer PRIVATE 
        nano_prc SDL3-shared
    )
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/glm" 
    "${CMAKE_CURRENT_BINARY_DIR}/glm_build"
)
target_include_directories(mobile_viewer PRIVATE "src")
target_link_libraries(mobile_viewer PRIVATE glm::glm-header-only)
