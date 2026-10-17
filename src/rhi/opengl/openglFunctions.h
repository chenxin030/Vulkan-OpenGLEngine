#pragma once

#include <cstddef>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// ---------------------------------------------------------------------------
// 最小化的 OpenGL 入口点加载器。

// 为什么不使用 GLAD 或 GLEW
// --------------------
// GLAD 在配置时需要 Python 来生成加载器。GLEW 的 git 仓库
// 并不包含生成的 src/glew.c，所以不能直接使用 FetchContent 获取。
// 这都不是不可行的——它们只是为一个实际 GL 调用只有几十次的后端增加了两个额外的可变部分。
//
// 加载器到底是什么
// -------------------------
// 在 Windows 上，opengl32.lib 只导出 OpenGL 1.1 的函数。更新的功能
// 都在驱动程序的 ICD 中，需要在运行时通过
// wglGetProcAddress 获取，而 GLFW 把它封装成 glfwGetProcAddress。因此，“加载器”
// 只是一个函数指针表，加上一个循环来填充它而已。GLAD 并没有什么神奇的地方；
// 它就是这样，被简化到这个后端使用的程度。

// 类型和枚举值在这里声明，而不是包含，因为
// <GL/glcorearb.h> 并不属于 Windows SDK，<GL/gl.h> 是 OpenGL 1.1。
// 这些值来自 Khronos 注册表，并不会改变。
// ---------------------------------------------------------------------------

// --- types ------------------------------------------------------------------
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef float GLfloat;
typedef void GLvoid;
typedef char GLchar;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

// --- enums ------------------------------------------------------------------
// 只有这个后端实际命名的常量。添加一个只需一行修改；
// 复制整个注册表就不是一行的事了。
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_NONE 0
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_RGBA8 0x8058
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VIEWPORT_DIMS 0x0D3A

#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4

#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_SHORT 0x1403
#define GL_TRIANGLES 0x0004

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_RGBA32F 0x8814
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
// sRGB，而不是普通的 RGBA8。采样器在读取时会转换为线性，
// GL_FRAMEBUFFER_SRGB 在写入时再转换回来，这和 Vulkan 后端从 VK_FORMAT_R8G8B8A8_SRGB 加上 sRGB 交换链得到的一对转换是一样的。
// 如果其中一半做错了，第二个后端看起来可能“差不多对”，但显然会显得洗白或灰暗。
#define GL_SRGB8_ALPHA8 0x8C43

#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F

// --- entry points -----------------------------------------------------------
//
// 一个 X-macro 列表：相同的列表生成成员、加载循环和缺失入口报告，所以这三者不会出现偏差。
// 如果手写的加载函数漏掉了某个条目，就会留下一个空指针，导致在绘制调用时失败，而不是在加载时失败。
#define GL_FUNCTION_LIST(X)                                          \
    X(glClearColor, void, (GLfloat, GLfloat, GLfloat, GLfloat))      \
    X(glClear, void, (GLbitfield))                                   \
    X(glEnable, void, (GLenum))                                      \
    X(glDisable, void, (GLenum))                                     \
    X(glFrontFace, void, (GLenum))                                   \
    X(glUniform4fv, void, (GLint, GLsizei, const GLfloat*))           \
    X(glCullFace, void, (GLenum))                                    \
    X(glGetIntegerv, void, (GLenum pname, GLint* data))            \
    X(glViewport, void, (GLint, GLint, GLsizei, GLsizei))            \
    X(glGetString, const unsigned char*, (GLenum))                   \
    X(glFinish, void, ())                                            \
    X(glGetError, GLenum, ())                                        \
    X(glGenBuffers, void, (GLsizei, GLuint*))                        \
    X(glBindBuffer, void, (GLenum, GLuint))                          \
    X(glBufferData, void, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(glBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(glBindBufferBase, void, (GLenum, GLuint, GLuint))              \
    X(glDeleteBuffers, void, (GLsizei, const GLuint*))               \
    X(glGenVertexArrays, void, (GLsizei, GLuint*))                   \
    X(glBindVertexArray, void, (GLuint))                             \
    X(glDeleteVertexArrays, void, (GLsizei, const GLuint*))          \
    X(glEnableVertexAttribArray, void, (GLuint))                     \
    X(glVertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(glDrawElements, void, (GLenum, GLsizei, GLenum, const void*))  \
    X(glDrawArrays, void, (GLenum, GLint, GLsizei))                  \
    X(glCreateShader, GLuint, (GLenum))                              \
    X(glShaderSource, void, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(glCompileShader, void, (GLuint))                               \
    X(glGetShaderiv, void, (GLuint, GLenum, GLint*))                 \
    X(glGetShaderInfoLog, void, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(glDeleteShader, void, (GLuint))                                \
    X(glCreateProgram, GLuint, ())                                   \
    X(glAttachShader, void, (GLuint, GLuint))                        \
    X(glLinkProgram, void, (GLuint))                                 \
    X(glGetProgramiv, void, (GLuint, GLenum, GLint*))                \
    X(glGetProgramInfoLog, void, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(glUseProgram, void, (GLuint))                                  \
    X(glDeleteProgram, void, (GLuint))                               \
    X(glGetUniformLocation, GLint, (GLuint, const GLchar*))          \
    X(glUniformMatrix4fv, void, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(glUniform3fv, void, (GLint, GLsizei, const GLfloat*))             \
    X(glUniform1i, void, (GLint, GLint))                             \
    X(glGenTextures, void, (GLsizei, GLuint*))                       \
    X(glBindTexture, void, (GLenum, GLuint))                         \
    X(glTexImage2D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(glTexParameteri, void, (GLenum, GLenum, GLint))                \
    X(glGenerateMipmap, void, (GLenum))                              \
    X(glActiveTexture, void, (GLenum))                               \
    X(glDeleteTextures, void, (GLsizei, const GLuint*))                 \
    X(glGenFramebuffers, void, (GLsizei, GLuint*))                      \
    X(glBindFramebuffer, void, (GLenum, GLuint))                        \
    X(glFramebufferTexture2D, void, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(glCheckFramebufferStatus, GLenum, (GLenum))                       \
    X(glDeleteFramebuffers, void, (GLsizei, const GLuint*))              \
    X(glDrawBuffer, void, (GLenum))                                     \
    X(glReadBuffer, void, (GLenum))

struct GlFunctions
{
#define GL_DECLARE_MEMBER(name, returnType, parameters) returnType (*name) parameters = nullptr;
    GL_FUNCTION_LIST(GL_DECLARE_MEMBER)
#undef GL_DECLARE_MEMBER

        // 将每个入口点解析为当前的上下文。当运行此代码时，必须有一个 GL 上下文处于当前状态，而且这些指针仅对该上下文所属的进程有效
        // 这也是为什么它不是在启动时全局初始化的原因。
    bool Load()
    {
        bool allResolved = true;

#define GL_LOAD_MEMBER(name, returnType, parameters)                              \
        name = reinterpret_cast<returnType (*) parameters>(glfwGetProcAddress(#name)); \
        allResolved = allResolved && (name != nullptr);
        GL_FUNCTION_LIST(GL_LOAD_MEMBER)
#undef GL_LOAD_MEMBER

            return allResolved;
    }

    // 命名无法解析的第一个入口点，用于错误消息。
    const char* FirstMissing() const
    {
#define GL_CHECK_MEMBER(name, returnType, parameters) \
        if (name == nullptr) { return #name; }
        GL_FUNCTION_LIST(GL_CHECK_MEMBER)
#undef GL_CHECK_MEMBER
            return "none";
    }
};
