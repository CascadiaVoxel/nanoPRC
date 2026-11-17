#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>

// OpenGL ES 3.0 Constants missing from SDL_opengles2.h
#ifndef GL_RED
#define GL_RED                          0x1903
#define GL_RG                           0x8227
#define GL_R8                           0x8229
#define GL_RG8                          0x822B
#define GL_RGB8                         0x8051
#define GL_RGBA8                        0x8058
#define GL_TEXTURE_SWIZZLE_R            0x8E42
#define GL_TEXTURE_SWIZZLE_G            0x8E43
#define GL_TEXTURE_SWIZZLE_B            0x8E44
#define GL_TEXTURE_SWIZZLE_A            0x8E45
#endif


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

typedef void (GL_APIENTRY *PFNGLGENERATEMIPMAPPROC) (GLenum target);
typedef void (GL_APIENTRY *PFNGLPIXELSTOREIPROC) (GLenum pname, GLint param);

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

static PFNGLGENERATEMIPMAPPROC win_glGenerateMipmap = nullptr;
static PFNGLPIXELSTOREIPROC win_glPixelStorei = nullptr;

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

#define glGenerateMipmap win_glGenerateMipmap
#define glPixelStorei win_glPixelStorei

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

    win_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmap");
    win_glPixelStorei = (PFNGLPIXELSTOREIPROC)SDL_GL_GetProcAddress("glPixelStorei");
}
#else
// Android and iOS map these natively at compile time via standard system symbols
inline void ResolveGLES3Extensions() {}
#endif