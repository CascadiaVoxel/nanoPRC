#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h> // Includes cross-platform GLES profiles
#include <prc_api.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>

// ============================================================================
// CROSS-PLATFORM OPENGL ES 3.0 FUNCTION POINTER RESOLUTION (WINDOWS ENABLE)
// ============================================================================
#if defined(SDL_PLATFORM_WIN32)
// VAO Function Layout Profiles
typedef void (GL_APIENTRY *PFNGLGENVERTEXARRAYSOESPROC) (GLsizei n, GLuint *arrays);
typedef void (GL_APIENTRY *PFNGLBINDVERTEXARRAYOESPROC) (GLuint array);
typedef void (GL_APIENTRY *PFNGLDELETEVERTEXARRAYSOESPROC) (GLsizei n, const GLuint *arrays);

// Core VBO / Vertex Attribute Pipeline Function Profiles
typedef void (GL_APIENTRY *PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (GL_APIENTRY *PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (GL_APIENTRY *PFNGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (GL_APIENTRY *PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void (GL_APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (GL_APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

// Isolated pointer storage instances avoiding global name collisions with gl.h symbols
static PFNGLGENVERTEXARRAYSOESPROC win_glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYOESPROC win_glBindVertexArray = nullptr;
static PFNGLDELETEVERTEXARRAYSOESPROC win_glDeleteVertexArrays = nullptr;
static PFNGLGENBUFFERSPROC win_glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC win_glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC win_glBufferData = nullptr;
static PFNGLDELETEBUFFERSPROC win_glDeleteBuffers = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC win_glEnableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC win_glVertexAttribPointer = nullptr;

// Seamless macro translation mappings targeting the isolated function pointers exclusively on Windows
#define glGenVertexArrays win_glGenVertexArrays
#define glBindVertexArray win_glBindVertexArray
#define glDeleteVertexArrays win_glDeleteVertexArrays
#define glGenBuffers win_glGenBuffers
#define glBindBuffer win_glBindBuffer
#define glBufferData win_glBufferData
#define glDeleteBuffers win_glDeleteBuffers
#define glEnableVertexAttribArray win_glEnableVertexAttribArray
#define glVertexAttribPointer win_glVertexAttribPointer

inline void ResolveGLES3Extensions() {
    win_glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    if (!win_glGenVertexArrays) win_glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glGenVertexArraysOES");

    win_glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    if (!win_glBindVertexArray) win_glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)SDL_GL_GetProcAddress("glBindVertexArrayOES");

    win_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glDeleteVertexArrays");
    if (!win_glDeleteVertexArrays) win_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glDeleteVertexArraysOES");

    win_glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    win_glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    win_glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    win_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
    win_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    win_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
}
#else
// Android and iOS map these natively at compile time via standard system symbols
inline void ResolveGLES3Extensions() {}
#endif
// ============================================================================

// Unified Engine Runtime States
enum class EngineState
{
    Uninitialized,
    ExtractingAssets,
    ParsingDataStructure,
    ExtractingTessellation,
    MarshallingToGPU,
    ActiveRendering,
    ErrorState
};

// Fixed interleaved structural layout specifying vector dimensions for GLES 3.0
struct GLESVertex
{
    float position[3]; // x, y, z
    float normal[3];   // nx, ny, nz
    float color[4];    // r, g, b, a
};

struct MobileEngineContext
{
    EngineState state = EngineState::Uninitialized;
    std::string activeResolvedPath = "";

    // Public C API instances
    prc_context *prcCtx = nullptr;
    prc_api_data prcData = nullptr;
    prc_api_product *productTreeRoot = nullptr;
    prc_api_tess *surfaceTessArray = nullptr;
    prc_api_tess *lineTessArray = nullptr;
    uint32_t numSurfaceTess = 0;
    uint32_t numLineTess = 0;

    // OpenGL ES Vertex Object Registry
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

// Platform-agnostic asset extractor: ensures internal APK/bundle assets yield absolute file paths
bool ResolveCrossPlatformAssetPath(const char *relativeAssetName, std::string &outAbsolutePath)
{
#if defined(SDL_PLATFORM_ANDROID)
    const char *internalWriteDir = SDL_GetPrefPath("com.cascadvoxel", "nanoprc_viewer");
    if (!internalWriteDir) return false;

    outAbsolutePath = std::string(internalWriteDir) + relativeAssetName;

    SDL_FileInfo fileInfo;
    if (SDL_GetPathInfo(outAbsolutePath.c_str(), &fileInfo)) {
        return true;
    }

    std::string assetResourcePath = std::string("assets/") + relativeAssetName;
    SDL_IOStream *assetIn = SDL_IOFromFile(assetResourcePath.c_str(), "rb");
    if (!assetIn) return false;

    SDL_IOStream *cacheOut = SDL_IOFromFile(outAbsolutePath.c_str(), "wb");
    if (!cacheOut) {
        SDL_CloseIO(assetIn);
        return false;
    }

    const size_t bufferSize = 4096;
    std::vector<uint8_t> buffer(bufferSize);
    size_t bytesRead = 0;
    while ((bytesRead = SDL_ReadIO(assetIn, buffer.data(), bufferSize)) > 0) {
        SDL_WriteIO(cacheOut, buffer.data(), bytesRead);
    }

    SDL_CloseIO(assetIn);
    SDL_CloseIO(cacheOut);
    return true;
#else
    outAbsolutePath = relativeAssetName;
    return true;
#endif
}

void ProcessPRCGeometry(MobileEngineContext *engine)
{
    if (!engine || engine->state != EngineState::ParsingDataStructure) return;

    uint32_t numParts = 0, numProducts = 0, numMarkups = 0;
    if (prc_api_prep_model_tree(engine->prcCtx, engine->prcData, &numParts,
                                &numProducts, &numMarkups) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }

    if (prc_api_create_model_tree(engine->prcCtx, engine->prcData, &engine->productTreeRoot,
                                  numParts, numProducts, numMarkups) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }

    engine->state = EngineState::ExtractingTessellation;

    uint32_t numExactGeom = 0;
    if (prc_api_get_number_tessellations(engine->prcCtx, engine->prcData, engine->productTreeRoot,
        &engine->numSurfaceTess, &engine->numLineTess, &numExactGeom, 0) < 0)
    {
        engine->state = EngineState::ErrorState;
        return;
    }

    if (engine->numSurfaceTess > 0)
    {
        engine->surfaceTessArray = (prc_api_tess *)SDL_calloc(engine->numSurfaceTess, sizeof(prc_api_tess));
    }
    if (engine->numLineTess > 0)
    {
        engine->lineTessArray = (prc_api_tess *)SDL_calloc(engine->numLineTess, sizeof(prc_api_tess));
    }

    for (uint32_t i = 0; i < engine->numSurfaceTess; ++i)
    {
        uint8_t hasLineCompanion = 0;
        prc_api_initialize_tessellation(engine->prcCtx, engine->prcData, engine->productTreeRoot, i,
            &engine->surfaceTessArray[i], &engine->lineTessArray[i], &hasLineCompanion);
    }

    engine->state = EngineState::MarshallingToGPU;
}

void GenerateGLESBuffers(MobileEngineContext *engine)
{
    if (!engine || engine->state != EngineState::MarshallingToGPU) return;

    std::vector<GLESVertex> extractedVertices;
    std::vector<uint32_t> extractedIndices;
    uint32_t runningVertexOffset = 0;

    for (uint32_t t = 0; t < engine->numSurfaceTess; ++t)
    {
        prc_api_tess *tess = &engine->surfaceTessArray[t];
        uint32_t numFaces = prc_api_get_number_faces(engine->prcCtx, engine->prcData, t);

        for (uint32_t f = 0; f < numFaces; ++f)
        {
            if (prc_api_skip_face(engine->prcCtx, tess, f)) continue;

            uint32_t faceVertCount = 0;
            prc_api_vertex *rawVerts = nullptr;
            if (prc_api_get_face_vertices(engine->prcCtx, tess, f, &faceVertCount, &rawVerts) == 0)
            {
                for (uint32_t v = 0; v < faceVertCount; ++v)
                {
                    GLESVertex vertex;
                    std::memcpy(vertex.position, rawVerts[v].position, sizeof(float) * 3);
                    std::memcpy(vertex.normal, rawVerts[v].normal, sizeof(float) * 3);
                    std::memcpy(vertex.color, rawVerts[v].color, sizeof(float) * 4);
                    extractedVertices.push_back(vertex);
                }

                size_t numPrims = prc_api_get_num_graphics_primitives(engine->prcCtx, engine->prcData, tess);
                for (size_t p = 0; p < numPrims; ++p)
                {
                    prc_api_graphic_primitive prim;
                    if (prc_api_get_graphics_primitive(engine->prcCtx, engine->prcData, tess, f, p, &prim) == 0)
                    {
                        for (size_t idx = 0; idx < prim.num_indices; ++idx)
                        {
                            extractedIndices.push_back(prim.indices[idx] + runningVertexOffset);
                        }
                    }
                }
                runningVertexOffset += faceVertCount;
            }
        }
    }

    if (!extractedVertices.empty() && !extractedIndices.empty())
    {
        glGenVertexArrays(1, &engine->vao);
        glGenBuffers(1, &engine->vbo);
        glGenBuffers(1, &engine->ebo);
        glBindVertexArray(engine->vao);
        glBindBuffer(GL_ARRAY_BUFFER, engine->vbo);
        glBufferData(GL_ARRAY_BUFFER, extractedVertices.size() * sizeof(GLESVertex), extractedVertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, engine->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, extractedIndices.size() * sizeof(uint32_t), extractedIndices.data(), GL_STATIC_DRAW);
        // Position Attribute Setup
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, position));
        // Normal Attribute Setup
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, normal));
        // Color Attribute Setup
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLESVertex), (void *)offsetof(GLESVertex, color));
        engine->indexCount = static_cast<GLsizei>(extractedIndices.size()); // <-- Fixed line here
        glBindVertexArray(0);
        engine->state = EngineState::ActiveRendering;
    }
    else
    {
        engine->state = EngineState::ErrorState;
    }
}

void CleanEngineResources(MobileEngineContext *engine)
{
    if (!engine)
        return;

    if (engine->prcCtx && engine->prcData)
    {
        prc_api_release_data( engine->prcCtx, engine->prcData,
            engine->surfaceTessArray, engine->numSurfaceTess,
            engine->lineTessArray, engine->numLineTess,
            nullptr, 0, engine->productTreeRoot);

        if (engine->surfaceTessArray)
            SDL_free(engine->surfaceTessArray);
        if (engine->lineTessArray)
            SDL_free(engine->lineTessArray);
    }

    if (engine->prcCtx)
        prc_api_release_context(engine->prcCtx);
    if (engine->vao)
        glDeleteVertexArrays(1, &engine->vao);
    if (engine->vbo)
        glDeleteBuffers(1, &engine->vbo);
    if (engine->ebo)
        glDeleteBuffers(1, &engine->ebo);
}
int main(int argc, char *argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window *window = SDL_CreateWindow(
        "nanoPRC 3D PDF Viewer", 800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!window)
    {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);

    if (!glContext)
    {
        std::cerr << "GLES Context creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Resolve Vertex Array and Buffer extension methods for the Windows environment safely
    ResolveGLES3Extensions();
    glEnable(GL_DEPTH_TEST);
    MobileEngineContext engine;
    engine.prcCtx = prc_api_new_context(nullptr);
    if (engine.prcCtx)
    {
        std::cout << "nanoPRC parser context successfully active inside mobile loop!" << std::endl;
        engine.state = EngineState::ExtractingAssets;
    }
    else
    {
        engine.state = EngineState::ErrorState;
    }
    if (engine.state == EngineState::ExtractingAssets)
    {
        if (ResolveCrossPlatformAssetPath("cube.pdf", engine.activeResolvedPath))
        {
            engine.prcData = prc_api_open_contents(engine.prcCtx, engine.activeResolvedPath.c_str());
            if (engine.prcData)
            {
                engine.state = EngineState::ParsingDataStructure;
            }
            else
            {
                prc_api_print_error_stack(engine.prcCtx);
                engine.state = EngineState::ErrorState;
            }
        }
        else {
            engine.state = EngineState::ErrorState;
        }
    }

    bool running = true;
    SDL_Event event;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
        }

        if (engine.state == EngineState::ParsingDataStructure)
        {
            ProcessPRCGeometry(&engine);
        }

        if (engine.state == EngineState::MarshallingToGPU)
        {
            GenerateGLESBuffers(&engine);
        }

        glClearColor(0.1f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (engine.state == EngineState::ActiveRendering)
        {
            glBindVertexArray(engine.vao);
            glDrawElements(GL_TRIANGLES, engine.indexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }
        SDL_GL_SwapWindow(window);
    }

    CleanEngineResources(&engine);
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

