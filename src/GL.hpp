#pragma once

// ════════════════════════════════════════════════════════════════════════
//  GL.hpp — Accès direct à OpenGL 3.3 Core, sans dépendance externe
//
//  macOS   : <OpenGL/gl3.h> expose déjà tout le Core 3.3 → include direct.
//  Ailleurs: opengl32/libGL n'expose que GL 1.1, il faut résoudre les
//            pointeurs de fonction. On le fait via glfwGetProcAddress
//            (qui gère aussi le fallback GL 1.1 sous Windows).
//
//  Esprit « direct API access » : pas de couche d'abstraction, juste les
//  symboles dont le projet a réellement besoin.
// ════════════════════════════════════════════════════════════════════════

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION
#  endif
#  include <OpenGL/gl3.h>

inline bool glLoadFunctions() { return true; }   // rien à faire

#else
// ── Types Khronos ────────────────────────────────────────────────────────
#include <cstddef>
#include <cstdint>

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef void          GLvoid;
typedef int           GLint;
typedef unsigned int  GLuint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef double        GLdouble;
typedef char          GLchar;
typedef std::intptr_t GLintptr;
typedef std::ptrdiff_t GLsizeiptr;

// ── Constantes utilisées par le projet ───────────────────────────────────
#define GL_FALSE                          0
#define GL_TRUE                           1
#define GL_TRIANGLES                      0x0004
#define GL_LINE_STRIP                     0x0003
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_FLOAT                          0x1406
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_ARRAY_BUFFER                   0x8892
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82

// ── Déclaration des pointeurs ────────────────────────────────────────────
#define GL_FUNCS(X)                                                            \
  X(void,   Viewport,              (GLint,GLint,GLsizei,GLsizei))              \
  X(void,   ClearColor,            (GLfloat,GLfloat,GLfloat,GLfloat))          \
  X(void,   Clear,                 (GLbitfield))                               \
  X(void,   Enable,                (GLenum))                                   \
  X(void,   Disable,               (GLenum))                                   \
  X(void,   BlendFunc,             (GLenum,GLenum))                            \
  X(void,   DrawArrays,            (GLenum,GLint,GLsizei))                     \
  X(GLuint, CreateShader,          (GLenum))                                   \
  X(void,   ShaderSource,          (GLuint,GLsizei,const GLchar* const*,const GLint*)) \
  X(void,   CompileShader,         (GLuint))                                   \
  X(void,   GetShaderiv,           (GLuint,GLenum,GLint*))                     \
  X(void,   GetShaderInfoLog,      (GLuint,GLsizei,GLsizei*,GLchar*))          \
  X(void,   DeleteShader,          (GLuint))                                   \
  X(GLuint, CreateProgram,         (void))                                     \
  X(void,   AttachShader,          (GLuint,GLuint))                            \
  X(void,   LinkProgram,           (GLuint))                                   \
  X(void,   GetProgramiv,          (GLuint,GLenum,GLint*))                     \
  X(void,   GetProgramInfoLog,     (GLuint,GLsizei,GLsizei*,GLchar*))          \
  X(void,   UseProgram,            (GLuint))                                   \
  X(void,   DeleteProgram,         (GLuint))                                   \
  X(GLint,  GetUniformLocation,    (GLuint,const GLchar*))                     \
  X(void,   Uniform1i,             (GLint,GLint))                              \
  X(void,   Uniform1f,             (GLint,GLfloat))                            \
  X(void,   Uniform3fv,            (GLint,GLsizei,const GLfloat*))             \
  X(void,   Uniform4fv,            (GLint,GLsizei,const GLfloat*))             \
  X(void,   UniformMatrix4fv,      (GLint,GLsizei,GLboolean,const GLfloat*))   \
  X(void,   GenVertexArrays,       (GLsizei,GLuint*))                          \
  X(void,   BindVertexArray,       (GLuint))                                   \
  X(void,   DeleteVertexArrays,    (GLsizei,const GLuint*))                    \
  X(void,   GenBuffers,            (GLsizei,GLuint*))                          \
  X(void,   BindBuffer,            (GLenum,GLuint))                            \
  X(void,   BufferData,            (GLenum,GLsizeiptr,const void*,GLenum))     \
  X(void,   BufferSubData,         (GLenum,GLintptr,GLsizeiptr,const void*))   \
  X(void,   DeleteBuffers,         (GLsizei,const GLuint*))                    \
  X(void,   EnableVertexAttribArray,(GLuint))                                  \
  X(void,   VertexAttribPointer,   (GLuint,GLint,GLenum,GLboolean,GLsizei,const void*))

#define GL_DECLARE(ret, name, args) \
    typedef ret (*PFN_gl##name) args; inline PFN_gl##name gl##name = nullptr;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE

// Résout tous les pointeurs. À appeler APRÈS glfwMakeContextCurrent.
// Retourne false si une fonction manque (driver trop ancien).
inline bool glLoadFunctions()
{
    bool ok = true;
#define GL_LOAD(ret, name, args)                                               \
    gl##name = (PFN_gl##name)glfwGetProcAddress("gl" #name);                   \
    if (!gl##name) ok = false;
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return ok;
}

#endif // __APPLE__
