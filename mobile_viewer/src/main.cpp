/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h> //  Provides all GLES/OES type profiles seamlessly.
#include <prc_api.h>

// Core C++ Standard Library
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>

// Modern GLM Setup
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>


// ============================================================================
// CROSS-PLATFORM OPENGL ES 3.0 FUNCTION POINTER RESOLUTION (WINDOWS/WEB)
// ============================================================================
#if defined(SDL_PLATFORM_WIN32) || defined(__EMSCRIPTEN__)
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

// GLES 3.0 Shader Pipeline Function Profiles
typedef GLuint(GL_APIENTRY *PFNGLCREATESHADERPROC) (GLenum type);
typedef void (GL_APIENTRY *PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void (GL_APIENTRY *PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint(GL_APIENTRY *PFNGLCREATEPROGRAMPROC) (void);
typedef void (GL_APIENTRY *PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (GL_APIENTRY *PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (GL_APIENTRY *PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (GL_APIENTRY *PFNGLDELETESHADERPROC) (GLuint shader);
typedef void (GL_APIENTRY *PFNGLDELETEPROGRAMPROC) (GLuint program);

typedef GLint(GL_APIENTRY *PFNGLGETUNIFORMLOCATION_PROC) (GLuint program, const GLchar *name);
typedef void (GL_APIENTRY *PFNGLUNIFORMMATRIX4FV_PROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

typedef void (GL_APIENTRY *PFNGLCLEARPROC) (GLbitfield mask);
typedef void (GL_APIENTRY *PFNGLCLEARCOLORPROC) (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (GL_APIENTRY *PFNGLDRAWELEMENTSPROC) (GLenum mode, GLsizei count, GLenum type, const void *indices);
typedef void (GL_APIENTRY *PFNGLENABLEPROC) (GLenum cap);

// Isolated pointer storage instances avoiding global name collisions
static PFNGLGENVERTEXARRAYSOESPROC win_glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYOESPROC win_glBindVertexArray = nullptr;
static PFNGLDELETEVERTEXARRAYSOESPROC win_glDeleteVertexArrays = nullptr;
static PFNGLGENBUFFERSPROC win_glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC win_glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC win_glBufferData = nullptr;
static PFNGLDELETEBUFFERSPROC win_glDeleteBuffers = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC win_glEnableVertexAttribArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC win_glVertexAttribPointer = nullptr;

static PFNGLCREATESHADERPROC win_glCreateShader = nullptr;
static PFNGLSHADERSOURCEPROC win_glShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC win_glCompileShader = nullptr;
static PFNGLCREATEPROGRAMPROC win_glCreateProgram = nullptr;
static PFNGLATTACHSHADERPROC win_glAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC win_glLinkProgram = nullptr;
static PFNGLUSEPROGRAMPROC win_glUseProgram = nullptr;
static PFNGLDELETESHADERPROC win_glDeleteShader = nullptr;
static PFNGLDELETEPROGRAMPROC win_glDeleteProgram = nullptr;
static PFNGLGETUNIFORMLOCATION_PROC win_glGetUniformLocation = nullptr;
static PFNGLUNIFORMMATRIX4FV_PROC win_glUniformMatrix4fv = nullptr;

static PFNGLCLEARPROC win_glClear = nullptr;
static PFNGLCLEARCOLORPROC win_glClearColor = nullptr;
static PFNGLDRAWELEMENTSPROC win_glDrawElements = nullptr;
static PFNGLENABLEPROC win_glEnable = nullptr;

#define glGenVertexArrays win_glGenVertexArrays
#define glBindVertexArray win_glBindVertexArray
#define glDeleteVertexArrays win_glDeleteVertexArrays
#define glGenBuffers win_glGenBuffers
#define glBindBuffer win_glBindBuffer
#define glBufferData win_glBufferData
#define glDeleteBuffers win_glDeleteBuffers
#define glEnableVertexAttribArray win_glEnableVertexAttribArray
#define glVertexAttribPointer win_glVertexAttribPointer

#define glCreateShader win_glCreateShader
#define glShaderSource win_glShaderSource
#define glCompileShader win_glCompileShader
#define glCreateProgram win_glCreateProgram
#define glAttachShader win_glAttachShader
#define glLinkProgram win_glLinkProgram
#define glUseProgram win_glUseProgram
#define glDeleteShader win_glDeleteShader
#define glDeleteProgram win_glDeleteProgram
#define glGetUniformLocation win_glGetUniformLocation
#define glUniformMatrix4fv win_glUniformMatrix4fv

// Secure macro bounds translation targets
#define glClear(mask)                 do { if(win_glClear) win_glClear(mask); } while(0)
#define glClearColor(r, g, b, a)      do { if(win_glClearColor) win_glClearColor(r, g, b, a); } while(0)
#define glDrawElements(m, c, t, i)    do { if(win_glDrawElements) win_glDrawElements(m, c, t, i); } while(0)
#define glEnable(cap)                 do { if(win_glEnable) win_glEnable(cap); } while(0)


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

    win_glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    win_glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    win_glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    win_glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    win_glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    win_glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    win_glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    win_glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    win_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");

    win_glGetUniformLocation = (PFNGLGETUNIFORMLOCATION_PROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    win_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FV_PROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");

    win_glClear = (PFNGLCLEARPROC)SDL_GL_GetProcAddress("glClear");
    win_glClearColor = (PFNGLCLEARCOLORPROC)SDL_GL_GetProcAddress("glClearColor");
    win_glDrawElements = (PFNGLDRAWELEMENTSPROC)SDL_GL_GetProcAddress("glDrawElements");
    win_glEnable = (PFNGLENABLEPROC)SDL_GL_GetProcAddress("glEnable");
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

static struct ActiveTouch {
    Uint64 id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool active = false;
} activeFingers[2]; // Track up to 2 active fingers for the pinch pipeline

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
    GLuint shaderProgram = 0; // Modern GLES program handler reference

    // --- 3D Trackball Interaction Space ---
    bool isInteracting = false;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    glm::mat4 objectOrientation = glm::mat4(1.0f);

    // --- Dynamic Zoom Space ---
    float cameraDistance = 20.0f; // Matches your initial 20.0f Z-position
    float minDistance = 2.0f;     // Prevents zooming inside the mesh
    float maxDistance = 80.0f;    // Prevents zooming infinitely far away

    // Multi-touch tracking tracking states
    bool isPinching = false;
    float initialPinchDistance = 0.0f;
    float initialCameraDistance = 0.0f;
};

const char *vertexShaderSource = "#version 300 es\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec3 aNormal;\n"
"layout(location = 2) in vec4 aColor;\n"
"uniform mat4 uMVP;\n" // Matrix uniform input
"out vec4 ourColor;\n"
"void main() {\n"
"    gl_Position = uMVP * vec4(aPos, 1.0);\n" // <-- Apply camera projection
"    ourColor = aColor;\n"
"}\n";

const char *fragmentShaderSource = "#version 300 es\n"
"precision mediump float;\n"
"in vec4 ourColor;\n"
"out vec4 fragColor;\n"
"void main() {\n"
"    fragColor = ourColor;\n"
"}\n";

GLuint CreateShaderProgram(const char *vsSource, const char *fsSource)
{
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vsSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fsSource, NULL);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shaderProgram;
}

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
    int code;
    if (!engine || engine->state != EngineState::ParsingDataStructure)
        return;
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
        prc_api_tess &tess = engine->surfaceTessArray[i];
        uint32_t nFaces = prc_api_get_number_faces(engine->prcCtx, engine->prcData, i);
        if (prc_api_get_tessellation_type(engine->prcCtx, engine->prcData, i) == PRC_API_TESS_3D_Compressed)
        {
            nFaces = 1;
        }
        tess.num_faces = nFaces;
        tess.tess_faces = new prc_api_face[nFaces];
        code = prc_api_initialize_tessellation(engine->prcCtx, engine->prcData, engine->productTreeRoot, i,
            &engine->surfaceTessArray[i], engine->numLineTess > 0 ? &engine->lineTessArray[i] : NULL, &hasLineCompanion);
        if (hasLineCompanion && engine->numLineTess > 0)
        {
            engine->lineTessArray[i].num_faces = nFaces;
            engine->lineTessArray[i].tess_faces = new prc_api_face[nFaces];
        }
        if (tess.type == PRC_API_TESS_3D_Wire ||
            tess.type == PRC_API_TESS_MarkUp)
        {
            code = prc_api_get_tessellation_vertices(engine->prcCtx, engine->prcData, i, 0, NULL, &tess);
        }
        else
        {
            for (uint32_t j = 0; j < tess.num_faces; j++)
            {
                code = prc_api_get_tessellation_vertices(engine->prcCtx, engine->prcData, i, j,
                    tess.tess_faces + j, &tess);
            }
        }
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
            if (prc_api_skip_face(engine->prcCtx, tess, f))
                continue;

            uint32_t faceVertCount = 0;
            prc_api_vertex *rawVerts = nullptr;
            if (prc_api_get_face_vertices(engine->prcCtx, tess, f, &faceVertCount, &rawVerts) == 0)
            {
                for (uint32_t v = 0; v < faceVertCount; ++v)
                {
                    rawVerts[v].color[0] = 1.0f; // Force Red
                    rawVerts[v].color[1] = 0.0f;
                    rawVerts[v].color[2] = 0.0f;
                    rawVerts[v].color[3] = 1.0f;
                    GLESVertex vertex;
                    std::memcpy(vertex.position, rawVerts[v].position, sizeof(float) * 3);
                    std::memcpy(vertex.normal, rawVerts[v].normal, sizeof(float) * 3);
                    std::memcpy(vertex.color, rawVerts[v].color, sizeof(float) * 4);
                    extractedVertices.push_back(vertex);
                    // Quick layout terminal print tracker to verify raw bounding ranges
                    std::cout << "Extracted Vert Vertex Coordinate Range -> [X]: " << vertex.position[0]
                        << ", [Y]: " << vertex.position[1] << ", [Z]: " << vertex.position[2] << std::endl;
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
        engine->indexCount = static_cast<GLsizei>(extractedIndices.size());
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

    if (engine->shaderProgram)
        glDeleteProgram(engine->shaderProgram);

    if (engine->prcCtx && engine->prcData)
    {
        prc_api_release_data(engine->prcCtx, engine->prcData,
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

inline glm::vec3 MapToTrackballSphere(float mouseX, float mouseY, float windowWidth, float windowHeight)
{
    // 1. Convert pixel space coordinates to Normalized Device Coordinates [-1.0 to 1.0]
    float x = (2.0f * mouseX) / windowWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowHeight; // Flip Y axis for OpenGL standard orientation

    // 2. Calculate distance from the center of the viewport
    float d = x * x + y * y;
    glm::vec3 vector(x, y, 0.0f);

    // 3. Project coordinates onto a sphere surface, fallback to a hyperbolic sheet if dragging far outside
    if (d <= 1.0f)
    {
        vector.z = std::sqrt(1.0f - d);
    }
    else
    {
        vector = glm::normalize(vector); // Snap to outer edge boundary edge
    }
    return vector;
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

    // 1. Resolve ALL driver function pointers instantly at runtime
    ResolveGLES3Extensions();

    // 2. Production Security Gate: Validate raw internal pointers directly 
    // to prove to the static analyzer that a null execution path is impossible.
#if defined(SDL_PLATFORM_WIN32) || defined(__EMSCRIPTEN__)
    if (!win_glGetUniformLocation || !win_glUniformMatrix4fv || !win_glEnable ||
        !win_glClear || !win_glClearColor || !win_glDrawElements)
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "FATAL: Graphics driver failed to export core OpenGL ES functions. Terminating.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1; // Abort entry path securely
    }
#endif

    // 3. Completely secure from null dereference, syntax errors, and linker tracking!
    glEnable(GL_DEPTH_TEST);

    // ====================================================================
    // REMOVED THE TWO MANUAL ASSIGNMENTS THAT WERE CRASHING / COLLIDING HERE
    // ====================================================================

    MobileEngineContext engine;
    engine.shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
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
        if (ResolveCrossPlatformAssetPath("triangle.pdf", engine.activeResolvedPath))
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
        else
        {
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

            // ================================================================
            // 1. MOBILE TOUCH EVENTS (ISOLATED GESTURE SPACE)
            // ================================================================
            else if (event.type == SDL_EVENT_FINGER_DOWN)
            {
                if (!activeFingers[0].active)
                {
                    activeFingers[0].id = event.tfinger.fingerID;
                    activeFingers[0].x = event.tfinger.x;
                    activeFingers[0].y = event.tfinger.y;
                    activeFingers[0].active = true;
                }
                else if (!activeFingers[1].active && activeFingers[0].id != event.tfinger.fingerID) {
                    activeFingers[1].id = event.tfinger.fingerID;
                    activeFingers[1].x = event.tfinger.x;
                    activeFingers[1].y = event.tfinger.y;
                    activeFingers[1].active = true;

                    engine.isPinching = true;
                    float dx = activeFingers[0].x - activeFingers[1].x;
                    float dy = activeFingers[0].y - activeFingers[1].y;
                    engine.initialPinchDistance = std::sqrt(dx * dx + dy * dy);
                    engine.initialCameraDistance = engine.cameraDistance;
                }
            }
            else if (event.type == SDL_EVENT_FINGER_UP)
            {
                if (activeFingers[0].active && activeFingers[0].id == event.tfinger.fingerID) {
                    activeFingers[0].active = false;
                }
                if (activeFingers[1].active && activeFingers[1].id == event.tfinger.fingerID) {
                    activeFingers[1].active = false;
                }

                if (!activeFingers[0].active || !activeFingers[1].active) {
                    engine.isPinching = false;
                }
            }
            else if (event.type == SDL_EVENT_FINGER_MOTION)
            {
                if (activeFingers[0].active && activeFingers[0].id == event.tfinger.fingerID) {
                    activeFingers[0].x = event.tfinger.x;
                    activeFingers[0].y = event.tfinger.y;
                }
                if (activeFingers[1].active && activeFingers[1].id == event.tfinger.fingerID) {
                    activeFingers[1].x = event.tfinger.x;
                    activeFingers[1].y = event.tfinger.y;
                }

                if (engine.isPinching && activeFingers[0].active && activeFingers[1].active)
                {
                    float dx = activeFingers[0].x - activeFingers[1].x;
                    float dy = activeFingers[0].y - activeFingers[1].y;
                    float currentPinchDistance = std::sqrt(dx * dx + dy * dy);

                    if (engine.initialPinchDistance > 0.001f) {
                        float scaleFactor = engine.initialPinchDistance / currentPinchDistance;
                        engine.cameraDistance = engine.initialCameraDistance * scaleFactor;
                        engine.cameraDistance = glm::clamp(engine.cameraDistance, engine.minDistance, engine.maxDistance);
                    }
                }
            }

            // ================================================================
            // 2. DESKTOP MOUSE EVENTS (SEPARATED WORKING ORBIT STRUCTURES)
            // ================================================================
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                engine.isInteracting = true;
                engine.lastMouseX = event.button.x;
                engine.lastMouseY = event.button.y;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                engine.isInteracting = false;
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                // Core trackball orbit block executes cleanly on desktop mouse movements
                if (engine.isInteracting)
                {
                    glm::vec3 va = MapToTrackballSphere(engine.lastMouseX, engine.lastMouseY, 800.0f, 600.0f);
                    glm::vec3 vb = MapToTrackballSphere(event.motion.x, event.motion.y, 800.0f, 600.0f);

                    float dot = glm::clamp(glm::dot(va, vb), -1.0f, 1.0f);
                    float angle = std::acos(dot) * 2.5f;

                    if (angle > 0.001f) {
                        glm::vec3 axis = glm::normalize(glm::cross(va, vb));
                        glm::mat4 incrementalRotation = glm::rotate(glm::mat4(1.0f), angle, axis);
                        engine.objectOrientation = incrementalRotation * engine.objectOrientation;
                    }
                    engine.lastMouseX = event.motion.x;
                    engine.lastMouseY = event.motion.y;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                engine.cameraDistance -= event.wheel.y * 1.5f;
                engine.cameraDistance = glm::clamp(engine.cameraDistance, engine.minDistance, engine.maxDistance);
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
            glUseProgram(engine.shaderProgram);

            // 1. Projections
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

            // 2. MODIFIED: Feed the dynamic distance state directly into the camera vector array
            glm::mat4 view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, engine.cameraDistance), // <-- Use your zoom state variable here!
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            // 3. Model Orientation from Trackball
            glm::mat4 model = engine.objectOrientation;

            // Combine matrices into MVP
            glm::mat4 mvp = projection * view * model;

            // 4. Uniform Upload
            GLint mvpLoc = glGetUniformLocation(engine.shaderProgram, "uMVP");
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));

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

