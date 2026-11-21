cmake_minimum_required(VERSION 3.16)
project(mobile_viewer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MOBILE_VIEWER_SOURCES
    "src/camera.cpp"
    "src/texture.cpp"
    "src/product.cpp"
    "src/scene.cpp"
    "src/mesh.cpp"
    "src/main.cpp"
    "src/shadow.cpp"
    "src/skybox.cpp"
    "src/shader.cpp"
    "src/composite.cpp"
    "../thirdparty/imgui/imgui.cpp"
	"../thirdparty/imgui/imgui_draw.cpp"
	"../thirdparty/imgui/imgui_widgets.cpp"
	"../thirdparty/imgui/imgui_tables.cpp"
	"../thirdparty/imgui/backends/imgui_impl_sdl3.cpp"
	"../thirdparty/imgui/backends/imgui_impl_opengl3.cpp"
)

# pack shaders into header files (guarded, skip missing source files)
message("Creating shader output")
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR src/shaders)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src/shaders)

set(GENERATOR_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../demos/viewer/cmake/shader_to_header.cmake)

# helper macro to add a shader only if the source exists
macro(add_shader_if_exists SRC_PATH OUT_REL HEADER_VAR VAR_NAME)
    if(EXISTS "${SRC_PATH}")
        set(OUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/${OUT_REL}")
        message("OUT_PATH for shader is ${OUT_PATH} for source ${SRC_PATH} with variable name ${VAR_NAME}")
        add_custom_command(
            OUTPUT ${OUT_PATH}
            COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=${SRC_PATH} -DOUTPUT_FILE=${OUT_PATH} -DVARIABLE_NAME=${VAR_NAME} -P ${GENERATOR_SCRIPT}
            DEPENDS ${SRC_PATH}
            COMMENT "Generating ${OUT_REL}"
        )
        list(APPEND GENERATED_SHADER_HEADERS ${OUT_PATH})
        set(${HEADER_VAR} ${OUT_REL} PARENT_SCOPE)
    else()
        message(WARNING "Shader source not found: ${SRC_PATH} -> skipping generation of ${OUT_REL}")
    endif()
endmacro()

# call the helper for each shader you want to generate
add_shader_if_exists("${SHADER_SRC_DIR}/generic.vert" "${SHADER_OUT_DIR}/generic.vert.h" GENERIC_VERT_HEADER generic_vert_source)
add_shader_if_exists("${SHADER_SRC_DIR}/generic.frag" "${SHADER_OUT_DIR}/generic.frag.h" GENERIC_FRAG_HEADER generic_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/skybox.vert" "${SHADER_OUT_DIR}/skybox.vert.h" SKYBOX_VERT_HEADER skybox_vert_source)
add_shader_if_exists("${SHADER_SRC_DIR}/skybox.frag" "${SHADER_OUT_DIR}/skybox.frag.h" SKYBOX_FRAG_HEADER skybox_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/shadow.vert" "${SHADER_OUT_DIR}/shadow.vert.h" SHADOW_VERT_HEADER shadow_vert_source)
add_shader_if_exists("${SHADER_SRC_DIR}/shadow.frag" "${SHADER_OUT_DIR}/shadow.frag.h" SHADOW_FRAG_HEADER shadow_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/composite.vert" "${SHADER_OUT_DIR}/composite.vert.h" COMPOSITE_VERT_HEADER composite_vert_source)
add_shader_if_exists("${SHADER_SRC_DIR}/composite.frag" "${SHADER_OUT_DIR}/composite.frag.h" COMPOSITE_FRAG_HEADER composite_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/gui.vert" "${SHADER_OUT_DIR}/gui.vert.h" GUI_VERT_HEADER gui_vert_source)
add_shader_if_exists("${SHADER_SRC_DIR}/gui.frag" "${SHADER_OUT_DIR}/gui.frag.h" GUI_FRAG_HEADER gui_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/downsample.frag" "${SHADER_OUT_DIR}/downsample.frag.h" DOWNSAMPLE_FRAG_HEADER downsample_frag_source)
add_shader_if_exists("${SHADER_SRC_DIR}/upsample.frag" "${SHADER_OUT_DIR}/upsample.frag.h" UPSAMPLE_FRAG_HEADER upsample_frag_source)

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
    #   variant -- --preload-file packages examples/triangle.pdf into the
    #   virtual filesystem (MEMFS) at /triangle.pdf ahead of main() running,
    #   so ResolveCrossPlatformAssetPath's existing relative-path branch
    #   (main.cpp currently opens "triangle.pdf", resolved against MEMFS's
    #   root cwd) finds it unmodified -- keep this filename in sync with
    #   whatever ResolveCrossPlatformAssetPath's caller in main.cpp actually
    #   asks for; it won't fail loudly if they drift apart; see the next
    #   comment.
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
        "-sASYNCIFY_STACK_SIZE=65536"
        "-sUSE_WEBGL2=1"
        "-sFULL_ES3=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY=1"
        "-sSTACK_SIZE=4194304"
        "-sEXPORTED_FUNCTIONS=[\"_main\",\"_OnWebFileDropped\"]"
        "-sEXPORTED_RUNTIME_METHODS=[\"FS\",\"ccall\",\"cwrap\"]"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=NanoPRCViewerModule"
        "--preload-file=${CMAKE_SOURCE_DIR}/examples/triangle.pdf@/triangle.pdf"
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

    # if none found, warn (optional)
if(NOT GENERATED_SHADER_HEADERS)
    message(WARNING "No shader headers will be generated: no shader source files found in ${SHADER_SRC_DIR}")
else()
    add_custom_target(generate_nano_prc_shaders ALL DEPENDS ${GENERATED_SHADER_HEADERS})
    add_dependencies(mobile_viewer generate_nano_prc_shaders)

    # Add the generated headers to the target sources and mark GENERATED
    target_sources(mobile_viewer PRIVATE ${GENERATED_SHADER_HEADERS})
    set_source_files_properties(${GENERATED_SHADER_HEADERS} PROPERTIES GENERATED TRUE)
endif()

target_include_directories(mobile_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/MatrixUtil/MatrixUtil/include)
target_include_directories(mobile_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/imgui)

target_include_directories(mobile_viewer PRIVATE
    "src"
    "${CMAKE_CURRENT_BINARY_DIR}/src")
