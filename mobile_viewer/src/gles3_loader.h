#pragma once
#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_ANDROID) || defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#elif defined(SDL_PLATFORM_IOS)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <SDL3/SDL_opengles2.h>
#endif

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

#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif

// ============================================================================
// CROSS-PLATFORM OPENGL ES FUNCTION POINTER RESOLUTION (WINDOWS/WEB)
// ============================================================================
#if defined(SDL_PLATFORM_WIN32)

// --- Existing Typedefs ---
typedef void (GL_APIENTRY *PFNGLGENVERTEXARRAYSOESPROC) (GLsizei n, GLuint *arrays);
typedef void (GL_APIENTRY *PFNGLBINDVERTEXARRAYOESPROC) (GLuint array);
typedef void (GL_APIENTRY *PFNGLDELETEVERTEXARRAYSOESPROC) (GLsizei n, const GLuint *arrays);

typedef void (GL_APIENTRY *PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (GL_APIENTRY *PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (GL_APIENTRY *PFNGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (GL_APIENTRY *PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void (GL_APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (GL_APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

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

// --- Newly Added Typedefs ---
typedef void (GL_APIENTRY *PFNGLBINDTEXTUREPROC) (GLenum target, GLuint texture);
typedef void (GL_APIENTRY *PFNGLDELETETEXTURESPROC) (GLsizei n, const GLuint *textures);
typedef void (GL_APIENTRY *PFNGLGENTEXTURESPROC) (GLsizei n, GLuint *textures);
typedef GLenum(GL_APIENTRY *PFNGLGETERRORPROC) (void);
typedef void (GL_APIENTRY *PFNGLTEXIMAGE2DPROC) (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void (GL_APIENTRY *PFNGLTEXPARAMETERIPROC) (GLenum target, GLenum pname, GLint param);
typedef void (GL_APIENTRY *PFNGLACTIVETEXTUREPROC) (GLenum texture);

typedef void (GL_APIENTRY *PFNGLBLENDEQUATIONPROC) (GLenum mode);
typedef void (GL_APIENTRY *PFNGLBLENDFUNCPROC) (GLenum sfactor, GLenum dfactor);
typedef void (GL_APIENTRY *PFNGLDEPTHFUNCPROC) (GLenum func);
typedef void (GL_APIENTRY *PFNGLDEPTHMASKPROC) (GLboolean flag);
typedef void (GL_APIENTRY *PFNGLDISABLEPROC) (GLenum cap);
typedef void (GL_APIENTRY *PFNGLVIEWPORTPROC) (GLint x, GLint y, GLsizei width, GLsizei height);
typedef const GLubyte *(GL_APIENTRY *PFNGLGETSTRINGPROC) (GLenum name);

typedef void (GL_APIENTRY *PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef GLenum(GL_APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC) (GLenum target);
typedef void (GL_APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC) (GLsizei n, const GLuint *framebuffers);
typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (GL_APIENTRY *PFNGLGENFRAMEBUFFERSPROC) (GLsizei n, GLuint *framebuffers);

typedef void (GL_APIENTRY *PFNGLBINDRENDERBUFFERPROC) (GLenum target, GLuint renderbuffer);
typedef void (GL_APIENTRY *PFNGLDELETERENDERBUFFERSPROC) (GLsizei n, const GLuint *renderbuffers);
typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC) (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (GL_APIENTRY *PFNGLGENRENDERBUFFERSPROC) (GLsizei n, GLuint *renderbuffers);
typedef void (GL_APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height);

typedef void (GL_APIENTRY *PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void (GL_APIENTRY *PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (GL_APIENTRY *PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef void (GL_APIENTRY *PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);

typedef void (GL_APIENTRY *PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (GL_APIENTRY *PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (GL_APIENTRY *PFNGLUNIFORM2FVPROC) (GLint location, GLsizei count, const GLfloat *value);
typedef void (GL_APIENTRY *PFNGLUNIFORM3FVPROC) (GLint location, GLsizei count, const GLfloat *value);

typedef void (GL_APIENTRY *PFNGLDRAWBUFFERSPROC) (GLsizei n, const GLenum *bufs);
typedef void (GL_APIENTRY *PFNGLDRAWARRAYSPROC) (GLenum mode, GLint first, GLsizei count);

// --- Shared Pointer Storage (Using a template struct trick so C++14 shares 1 instance across all .cpp files!) ---
template<typename T = void>
struct WinGLESGlobals {
    static PFNGLGENVERTEXARRAYSOESPROC p_glGenVertexArrays;
    static PFNGLBINDVERTEXARRAYOESPROC p_glBindVertexArray;
    static PFNGLDELETEVERTEXARRAYSOESPROC p_glDeleteVertexArrays;
    static PFNGLGENBUFFERSPROC p_glGenBuffers;
    static PFNGLBINDBUFFERPROC p_glBindBuffer;
    static PFNGLBUFFERDATAPROC p_glBufferData;
    static PFNGLDELETEBUFFERSPROC p_glDeleteBuffers;
    static PFNGLENABLEVERTEXATTRIBARRAYPROC p_glEnableVertexAttribArray;
    static PFNGLVERTEXATTRIBPOINTERPROC p_glVertexAttribPointer;

    static PFNGLCREATESHADERPROC p_glCreateShader;
    static PFNGLSHADERSOURCEPROC p_glShaderSource;
    static PFNGLCOMPILESHADERPROC p_glCompileShader;
    static PFNGLCREATEPROGRAMPROC p_glCreateProgram;
    static PFNGLATTACHSHADERPROC p_glAttachShader;
    static PFNGLLINKPROGRAMPROC p_glLinkProgram;
    static PFNGLUSEPROGRAMPROC p_glUseProgram;
    static PFNGLDELETESHADERPROC p_glDeleteShader;
    static PFNGLDELETEPROGRAMPROC p_glDeleteProgram;
    static PFNGLGETUNIFORMLOCATION_PROC p_glGetUniformLocation;
    static PFNGLUNIFORMMATRIX4FV_PROC p_glUniformMatrix4fv;

    static PFNGLCLEARPROC p_glClear;
    static PFNGLCLEARCOLORPROC p_glClearColor;
    static PFNGLDRAWELEMENTSPROC p_glDrawElements;
    static PFNGLENABLEPROC p_glEnable;
    static PFNGLGENERATEMIPMAPPROC p_glGenerateMipmap;
    static PFNGLPIXELSTOREIPROC p_glPixelStorei;

    static PFNGLBINDTEXTUREPROC p_glBindTexture;
    static PFNGLDELETETEXTURESPROC p_glDeleteTextures;
    static PFNGLGENTEXTURESPROC p_glGenTextures;
    static PFNGLGETERRORPROC p_glGetError;
    static PFNGLTEXIMAGE2DPROC p_glTexImage2D;
    static PFNGLTEXPARAMETERIPROC p_glTexParameteri;
    static PFNGLACTIVETEXTUREPROC p_glActiveTexture;

    static PFNGLBLENDEQUATIONPROC p_glBlendEquation;
    static PFNGLBLENDFUNCPROC p_glBlendFunc;
    static PFNGLDEPTHFUNCPROC p_glDepthFunc;
    static PFNGLDEPTHMASKPROC p_glDepthMask;
    static PFNGLDISABLEPROC p_glDisable;
    static PFNGLVIEWPORTPROC p_glViewport;
    static PFNGLGETSTRINGPROC p_glGetString;

    static PFNGLBINDFRAMEBUFFERPROC p_glBindFramebuffer;
    static PFNGLCHECKFRAMEBUFFERSTATUSPROC p_glCheckFramebufferStatus;
    static PFNGLDELETEFRAMEBUFFERSPROC p_glDeleteFramebuffers;
    static PFNGLFRAMEBUFFERTEXTURE2DPROC p_glFramebufferTexture2D;
    static PFNGLGENFRAMEBUFFERSPROC p_glGenFramebuffers;

    static PFNGLBINDRENDERBUFFERPROC p_glBindRenderbuffer;
    static PFNGLDELETERENDERBUFFERSPROC p_glDeleteRenderbuffers;
    static PFNGLFRAMEBUFFERRENDERBUFFERPROC p_glFramebufferRenderbuffer;
    static PFNGLGENRENDERBUFFERSPROC p_glGenRenderbuffers;
    static PFNGLRENDERBUFFERSTORAGEPROC p_glRenderbufferStorage;

    static PFNGLGETPROGRAMIVPROC p_glGetProgramiv;
    static PFNGLGETPROGRAMINFOLOGPROC p_glGetProgramInfoLog;
    static PFNGLGETSHADERIVPROC p_glGetShaderiv;
    static PFNGLGETSHADERINFOLOGPROC p_glGetShaderInfoLog;

    static PFNGLUNIFORM1FPROC p_glUniform1f;
    static PFNGLUNIFORM1IPROC p_glUniform1i;
    static PFNGLUNIFORM2FVPROC p_glUniform2fv;
    static PFNGLUNIFORM3FVPROC p_glUniform3fv;

    static PFNGLDRAWBUFFERSPROC p_glDrawBuffers;
    static PFNGLDRAWARRAYSPROC p_glDrawArrays;
};

template<typename T> PFNGLGENVERTEXARRAYSOESPROC WinGLESGlobals<T>::p_glGenVertexArrays = nullptr;
template<typename T> PFNGLBINDVERTEXARRAYOESPROC WinGLESGlobals<T>::p_glBindVertexArray = nullptr;
template<typename T> PFNGLDELETEVERTEXARRAYSOESPROC WinGLESGlobals<T>::p_glDeleteVertexArrays = nullptr;
template<typename T> PFNGLGENBUFFERSPROC WinGLESGlobals<T>::p_glGenBuffers = nullptr;
template<typename T> PFNGLBINDBUFFERPROC WinGLESGlobals<T>::p_glBindBuffer = nullptr;
template<typename T> PFNGLBUFFERDATAPROC WinGLESGlobals<T>::p_glBufferData = nullptr;
template<typename T> PFNGLDELETEBUFFERSPROC WinGLESGlobals<T>::p_glDeleteBuffers = nullptr;
template<typename T> PFNGLENABLEVERTEXATTRIBARRAYPROC WinGLESGlobals<T>::p_glEnableVertexAttribArray = nullptr;
template<typename T> PFNGLVERTEXATTRIBPOINTERPROC WinGLESGlobals<T>::p_glVertexAttribPointer = nullptr;
template<typename T> PFNGLCREATESHADERPROC WinGLESGlobals<T>::p_glCreateShader = nullptr;
template<typename T> PFNGLSHADERSOURCEPROC WinGLESGlobals<T>::p_glShaderSource = nullptr;
template<typename T> PFNGLCOMPILESHADERPROC WinGLESGlobals<T>::p_glCompileShader = nullptr;
template<typename T> PFNGLCREATEPROGRAMPROC WinGLESGlobals<T>::p_glCreateProgram = nullptr;
template<typename T> PFNGLATTACHSHADERPROC WinGLESGlobals<T>::p_glAttachShader = nullptr;
template<typename T> PFNGLLINKPROGRAMPROC WinGLESGlobals<T>::p_glLinkProgram = nullptr;
template<typename T> PFNGLUSEPROGRAMPROC WinGLESGlobals<T>::p_glUseProgram = nullptr;
template<typename T> PFNGLDELETESHADERPROC WinGLESGlobals<T>::p_glDeleteShader = nullptr;
template<typename T> PFNGLDELETEPROGRAMPROC WinGLESGlobals<T>::p_glDeleteProgram = nullptr;
template<typename T> PFNGLGETUNIFORMLOCATION_PROC WinGLESGlobals<T>::p_glGetUniformLocation = nullptr;
template<typename T> PFNGLUNIFORMMATRIX4FV_PROC WinGLESGlobals<T>::p_glUniformMatrix4fv = nullptr;
template<typename T> PFNGLCLEARPROC WinGLESGlobals<T>::p_glClear = nullptr;
template<typename T> PFNGLCLEARCOLORPROC WinGLESGlobals<T>::p_glClearColor = nullptr;
template<typename T> PFNGLDRAWELEMENTSPROC WinGLESGlobals<T>::p_glDrawElements = nullptr;
template<typename T> PFNGLENABLEPROC WinGLESGlobals<T>::p_glEnable = nullptr;
template<typename T> PFNGLGENERATEMIPMAPPROC WinGLESGlobals<T>::p_glGenerateMipmap = nullptr;
template<typename T> PFNGLPIXELSTOREIPROC WinGLESGlobals<T>::p_glPixelStorei = nullptr;
template<typename T> PFNGLBINDTEXTUREPROC WinGLESGlobals<T>::p_glBindTexture = nullptr;
template<typename T> PFNGLDELETETEXTURESPROC WinGLESGlobals<T>::p_glDeleteTextures = nullptr;
template<typename T> PFNGLGENTEXTURESPROC WinGLESGlobals<T>::p_glGenTextures = nullptr;
template<typename T> PFNGLGETERRORPROC WinGLESGlobals<T>::p_glGetError = nullptr;
template<typename T> PFNGLTEXIMAGE2DPROC WinGLESGlobals<T>::p_glTexImage2D = nullptr;
template<typename T> PFNGLTEXPARAMETERIPROC WinGLESGlobals<T>::p_glTexParameteri = nullptr;
template<typename T> PFNGLACTIVETEXTUREPROC WinGLESGlobals<T>::p_glActiveTexture = nullptr;
template<typename T> PFNGLBLENDEQUATIONPROC WinGLESGlobals<T>::p_glBlendEquation = nullptr;
template<typename T> PFNGLBLENDFUNCPROC WinGLESGlobals<T>::p_glBlendFunc = nullptr;
template<typename T> PFNGLDEPTHFUNCPROC WinGLESGlobals<T>::p_glDepthFunc = nullptr;
template<typename T> PFNGLDEPTHMASKPROC WinGLESGlobals<T>::p_glDepthMask = nullptr;
template<typename T> PFNGLDISABLEPROC WinGLESGlobals<T>::p_glDisable = nullptr;
template<typename T> PFNGLVIEWPORTPROC WinGLESGlobals<T>::p_glViewport = nullptr;
template<typename T> PFNGLGETSTRINGPROC WinGLESGlobals<T>::p_glGetString = nullptr;
template<typename T> PFNGLBINDFRAMEBUFFERPROC WinGLESGlobals<T>::p_glBindFramebuffer = nullptr;
template<typename T> PFNGLCHECKFRAMEBUFFERSTATUSPROC WinGLESGlobals<T>::p_glCheckFramebufferStatus = nullptr;
template<typename T> PFNGLDELETEFRAMEBUFFERSPROC WinGLESGlobals<T>::p_glDeleteFramebuffers = nullptr;
template<typename T> PFNGLFRAMEBUFFERTEXTURE2DPROC WinGLESGlobals<T>::p_glFramebufferTexture2D = nullptr;
template<typename T> PFNGLGENFRAMEBUFFERSPROC WinGLESGlobals<T>::p_glGenFramebuffers = nullptr;
template<typename T> PFNGLBINDRENDERBUFFERPROC WinGLESGlobals<T>::p_glBindRenderbuffer = nullptr;
template<typename T> PFNGLDELETERENDERBUFFERSPROC WinGLESGlobals<T>::p_glDeleteRenderbuffers = nullptr;
template<typename T> PFNGLFRAMEBUFFERRENDERBUFFERPROC WinGLESGlobals<T>::p_glFramebufferRenderbuffer = nullptr;
template<typename T> PFNGLGENRENDERBUFFERSPROC WinGLESGlobals<T>::p_glGenRenderbuffers = nullptr;
template<typename T> PFNGLRENDERBUFFERSTORAGEPROC WinGLESGlobals<T>::p_glRenderbufferStorage = nullptr;
template<typename T> PFNGLGETPROGRAMIVPROC WinGLESGlobals<T>::p_glGetProgramiv = nullptr;
template<typename T> PFNGLGETPROGRAMINFOLOGPROC WinGLESGlobals<T>::p_glGetProgramInfoLog = nullptr;
template<typename T> PFNGLGETSHADERIVPROC WinGLESGlobals<T>::p_glGetShaderiv = nullptr;
template<typename T> PFNGLGETSHADERINFOLOGPROC WinGLESGlobals<T>::p_glGetShaderInfoLog = nullptr;
template<typename T> PFNGLUNIFORM1FPROC WinGLESGlobals<T>::p_glUniform1f = nullptr;
template<typename T> PFNGLUNIFORM1IPROC WinGLESGlobals<T>::p_glUniform1i = nullptr;
template<typename T> PFNGLUNIFORM2FVPROC WinGLESGlobals<T>::p_glUniform2fv = nullptr;
template<typename T> PFNGLUNIFORM3FVPROC WinGLESGlobals<T>::p_glUniform3fv = nullptr;
template<typename T> PFNGLDRAWBUFFERSPROC WinGLESGlobals<T>::p_glDrawBuffers = nullptr;
template<typename T> PFNGLDRAWARRAYSPROC WinGLESGlobals<T>::p_glDrawArrays = nullptr;

// --- Macros ---
#define glGenVertexArrays WinGLESGlobals<>::p_glGenVertexArrays
#define glBindVertexArray WinGLESGlobals<>::p_glBindVertexArray
#define glDeleteVertexArrays WinGLESGlobals<>::p_glDeleteVertexArrays
#define glGenBuffers WinGLESGlobals<>::p_glGenBuffers
#define glBindBuffer WinGLESGlobals<>::p_glBindBuffer
#define glBufferData WinGLESGlobals<>::p_glBufferData
#define glDeleteBuffers WinGLESGlobals<>::p_glDeleteBuffers
#define glEnableVertexAttribArray WinGLESGlobals<>::p_glEnableVertexAttribArray
#define glVertexAttribPointer WinGLESGlobals<>::p_glVertexAttribPointer
#define glCreateShader WinGLESGlobals<>::p_glCreateShader
#define glShaderSource WinGLESGlobals<>::p_glShaderSource
#define glCompileShader WinGLESGlobals<>::p_glCompileShader
#define glCreateProgram WinGLESGlobals<>::p_glCreateProgram
#define glAttachShader WinGLESGlobals<>::p_glAttachShader
#define glLinkProgram WinGLESGlobals<>::p_glLinkProgram
#define glUseProgram WinGLESGlobals<>::p_glUseProgram
#define glDeleteShader WinGLESGlobals<>::p_glDeleteShader
#define glDeleteProgram WinGLESGlobals<>::p_glDeleteProgram
#define glGetUniformLocation WinGLESGlobals<>::p_glGetUniformLocation
#define glUniformMatrix4fv WinGLESGlobals<>::p_glUniformMatrix4fv
#define glGenerateMipmap WinGLESGlobals<>::p_glGenerateMipmap
#define glPixelStorei WinGLESGlobals<>::p_glPixelStorei
#define glBindTexture WinGLESGlobals<>::p_glBindTexture
#define glDeleteTextures WinGLESGlobals<>::p_glDeleteTextures
#define glGenTextures WinGLESGlobals<>::p_glGenTextures
#define glGetError WinGLESGlobals<>::p_glGetError
#define glTexImage2D WinGLESGlobals<>::p_glTexImage2D
#define glTexParameteri WinGLESGlobals<>::p_glTexParameteri
#define glActiveTexture WinGLESGlobals<>::p_glActiveTexture
#define glBlendEquation WinGLESGlobals<>::p_glBlendEquation
#define glBlendFunc WinGLESGlobals<>::p_glBlendFunc
#define glDepthFunc WinGLESGlobals<>::p_glDepthFunc
#define glDepthMask WinGLESGlobals<>::p_glDepthMask
#define glDisable WinGLESGlobals<>::p_glDisable
#define glViewport WinGLESGlobals<>::p_glViewport
#define glGetString WinGLESGlobals<>::p_glGetString
#define glBindFramebuffer WinGLESGlobals<>::p_glBindFramebuffer
#define glCheckFramebufferStatus WinGLESGlobals<>::p_glCheckFramebufferStatus
#define glDeleteFramebuffers WinGLESGlobals<>::p_glDeleteFramebuffers
#define glFramebufferTexture2D WinGLESGlobals<>::p_glFramebufferTexture2D
#define glGenFramebuffers WinGLESGlobals<>::p_glGenFramebuffers
#define glBindRenderbuffer WinGLESGlobals<>::p_glBindRenderbuffer
#define glDeleteRenderbuffers WinGLESGlobals<>::p_glDeleteRenderbuffers
#define glFramebufferRenderbuffer WinGLESGlobals<>::p_glFramebufferRenderbuffer
#define glGenRenderbuffers WinGLESGlobals<>::p_glGenRenderbuffers
#define glRenderbufferStorage WinGLESGlobals<>::p_glRenderbufferStorage
#define glGetProgramiv WinGLESGlobals<>::p_glGetProgramiv
#define glGetProgramInfoLog WinGLESGlobals<>::p_glGetProgramInfoLog
#define glGetShaderiv WinGLESGlobals<>::p_glGetShaderiv
#define glGetShaderInfoLog WinGLESGlobals<>::p_glGetShaderInfoLog
#define glUniform1f WinGLESGlobals<>::p_glUniform1f
#define glUniform1i WinGLESGlobals<>::p_glUniform1i
#define glUniform2fv WinGLESGlobals<>::p_glUniform2fv
#define glUniform3fv WinGLESGlobals<>::p_glUniform3fv
#define glDrawBuffers WinGLESGlobals<>::p_glDrawBuffers
#define glDrawArrays WinGLESGlobals<>::p_glDrawArrays
#define glClear(mask)                 do { if(WinGLESGlobals<>::p_glClear) WinGLESGlobals<>::p_glClear(mask); } while(0)
#define glClearColor(r, g, b, a)      do { if(WinGLESGlobals<>::p_glClearColor) WinGLESGlobals<>::p_glClearColor(r, g, b, a); } while(0)
#define glDrawElements(m, c, t, i)    do { if(WinGLESGlobals<>::p_glDrawElements) WinGLESGlobals<>::p_glDrawElements(m, c, t, i); } while(0)
#define glEnable(cap)                 do { if(WinGLESGlobals<>::p_glEnable) WinGLESGlobals<>::p_glEnable(cap); } while(0)

inline void ResolveGLES3Extensions() {
    using G = WinGLESGlobals<>;
    G::p_glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    if (!G::p_glGenVertexArrays) G::p_glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glGenVertexArraysOES");
    G::p_glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    if (!G::p_glBindVertexArray) G::p_glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)SDL_GL_GetProcAddress("glBindVertexArrayOES");
    G::p_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glDeleteVertexArrays");
    if (!G::p_glDeleteVertexArrays) G::p_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)SDL_GL_GetProcAddress("glDeleteVertexArraysOES");
    G::p_glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    G::p_glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    G::p_glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    G::p_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
    G::p_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    G::p_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
    G::p_glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    G::p_glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    G::p_glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    G::p_glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    G::p_glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    G::p_glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    G::p_glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    G::p_glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    G::p_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    G::p_glGetUniformLocation = (PFNGLGETUNIFORMLOCATION_PROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    G::p_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FV_PROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    G::p_glClear = (PFNGLCLEARPROC)SDL_GL_GetProcAddress("glClear");
    G::p_glClearColor = (PFNGLCLEARCOLORPROC)SDL_GL_GetProcAddress("glClearColor");
    G::p_glDrawElements = (PFNGLDRAWELEMENTSPROC)SDL_GL_GetProcAddress("glDrawElements");
    G::p_glEnable = (PFNGLENABLEPROC)SDL_GL_GetProcAddress("glEnable");
    G::p_glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmap");
    G::p_glPixelStorei = (PFNGLPIXELSTOREIPROC)SDL_GL_GetProcAddress("glPixelStorei");
    G::p_glBindTexture = (PFNGLBINDTEXTUREPROC)SDL_GL_GetProcAddress("glBindTexture");
    G::p_glDeleteTextures = (PFNGLDELETETEXTURESPROC)SDL_GL_GetProcAddress("glDeleteTextures");
    G::p_glGenTextures = (PFNGLGENTEXTURESPROC)SDL_GL_GetProcAddress("glGenTextures");
    G::p_glGetError = (PFNGLGETERRORPROC)SDL_GL_GetProcAddress("glGetError");
    G::p_glTexImage2D = (PFNGLTEXIMAGE2DPROC)SDL_GL_GetProcAddress("glTexImage2D");
    G::p_glTexParameteri = (PFNGLTEXPARAMETERIPROC)SDL_GL_GetProcAddress("glTexParameteri");
    G::p_glActiveTexture = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    G::p_glBlendEquation = (PFNGLBLENDEQUATIONPROC)SDL_GL_GetProcAddress("glBlendEquation");
    G::p_glBlendFunc = (PFNGLBLENDFUNCPROC)SDL_GL_GetProcAddress("glBlendFunc");
    G::p_glDepthFunc = (PFNGLDEPTHFUNCPROC)SDL_GL_GetProcAddress("glDepthFunc");
    G::p_glDepthMask = (PFNGLDEPTHMASKPROC)SDL_GL_GetProcAddress("glDepthMask");
    G::p_glDisable = (PFNGLDISABLEPROC)SDL_GL_GetProcAddress("glDisable");
    G::p_glViewport = (PFNGLVIEWPORTPROC)SDL_GL_GetProcAddress("glViewport");
    G::p_glGetString = (PFNGLGETSTRINGPROC)SDL_GL_GetProcAddress("glGetString");
    G::p_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    G::p_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    G::p_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    G::p_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    G::p_glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    G::p_glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
    G::p_glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteRenderbuffers");
    G::p_glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    G::p_glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
    G::p_glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
    G::p_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    G::p_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    G::p_glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    G::p_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    G::p_glUniform1f = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
    G::p_glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
    G::p_glUniform2fv = (PFNGLUNIFORM2FVPROC)SDL_GL_GetProcAddress("glUniform2fv");
    G::p_glUniform3fv = (PFNGLUNIFORM3FVPROC)SDL_GL_GetProcAddress("glUniform3fv");
    G::p_glDrawBuffers = (PFNGLDRAWBUFFERSPROC)SDL_GL_GetProcAddress("glDrawBuffers");
    G::p_glDrawArrays = (PFNGLDRAWARRAYSPROC)SDL_GL_GetProcAddress("glDrawArrays");
}
#else
inline void ResolveGLES3Extensions() {}
#endif