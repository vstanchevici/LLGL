/*
 * AndroidGLHardwareBuffer.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "AndroidGLHardwareBuffer.h"
#include "AndroidGLCore.h"
#include <LLGL/RenderSystemFlags.h>
#include <LLGL/Log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>


namespace LLGL
{


/* --- Internal procedures --- */

static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC   g_eglGetNativeClientBufferANDROID   = nullptr;
static PFNEGLCREATEIMAGEKHRPROC                 g_eglCreateImageKHR                 = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC                g_eglDestroyImageKHR                = nullptr;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC      g_glEGLImageTargetTexture2DOES      = nullptr;
static PFNEGLCREATESYNCKHRPROC                  g_eglCreateSyncKHR                  = nullptr;
static PFNEGLWAITSYNCKHRPROC                    g_eglWaitSyncKHR                    = nullptr;
static PFNEGLDESTROYSYNCKHRPROC                 g_eglDestroySyncKHR                 = nullptr;

static bool g_hardwareBufferProcsLoaded     = false;
static bool g_hardwareBufferSupported       = false;
static bool g_nativeFenceSyncSupported      = false;

// Returns true if the space separated list of extension names contains the specified name.
static bool HasExtensionName(const char* extensions, const char* name)
{
    if (extensions == nullptr)
        return false;

    const std::size_t nameLen = std::strlen(name);
    for (const char* s = extensions; (s = std::strstr(s, name)) != nullptr; s += nameLen)
    {
        /* Only accept whole words, e.g. "GL_OES_EGL_image_external" must not match "GL_OES_EGL_image_external_essl3" */
        const bool isStartOfWord    = (s == extensions || s[-1] == ' ');
        const bool isEndOfWord      = (s[nameLen] == ' ' || s[nameLen] == '\0');
        if (isStartOfWord && isEndOfWord)
            return true;
    }

    return false;
}

static bool HasGLExtension(const char* name)
{
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    for (GLint i = 0; i < numExtensions; ++i)
    {
        if (const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i))))
        {
            if (std::strcmp(extension, name) == 0)
                return true;
        }
    }
    return false;
}

template <typename T>
static bool LoadEGLProc(T& procAddr, const char* procName)
{
    procAddr = reinterpret_cast<T>(eglGetProcAddress(procName));
    return (procAddr != nullptr);
}

static void LoadHardwareBufferProcsOnce()
{
    if (g_hardwareBufferProcsLoaded)
        return;

    g_hardwareBufferProcsLoaded = true;

    EGLDisplay display = eglGetCurrentDisplay();
    if (display == EGL_NO_DISPLAY)
    {
        Log::Errorf("cannot query EGL extensions for Android hardware buffers without current EGL display\n");
        return;
    }

    const char* eglExtensions = eglQueryString(display, EGL_EXTENSIONS);

    /* Load procedures to import hardware buffers as external textures */
    g_hardwareBufferSupported =
    (
        HasGLExtension("GL_OES_EGL_image_external_essl3")                   &&
        HasExtensionName(eglExtensions, "EGL_KHR_image_base")               &&
        HasExtensionName(eglExtensions, "EGL_ANDROID_image_native_buffer")  &&
        HasExtensionName(eglExtensions, "EGL_ANDROID_get_native_client_buffer") &&
        LoadEGLProc(g_eglGetNativeClientBufferANDROID,  "eglGetNativeClientBufferANDROID")  &&
        LoadEGLProc(g_eglCreateImageKHR,                "eglCreateImageKHR")                &&
        LoadEGLProc(g_eglDestroyImageKHR,               "eglDestroyImageKHR")               &&
        LoadEGLProc(g_glEGLImageTargetTexture2DOES,     "glEGLImageTargetTexture2DOES")
    );

    /* Load procedures to wait on native fences on the GPU */
    g_nativeFenceSyncSupported =
    (
        HasExtensionName(eglExtensions, "EGL_ANDROID_native_fence_sync")    &&
        HasExtensionName(eglExtensions, "EGL_KHR_wait_sync")                &&
        LoadEGLProc(g_eglCreateSyncKHR,     "eglCreateSyncKHR")             &&
        LoadEGLProc(g_eglWaitSyncKHR,       "eglWaitSyncKHR")               &&
        LoadEGLProc(g_eglDestroySyncKHR,    "eglDestroySyncKHR")
    );
}


/* --- Global functions --- */

bool AndroidGLSupportsHardwareBuffers()
{
    LoadHardwareBufferProcsOnce();
    return g_hardwareBufferSupported;
}

// Maps the specified hardware buffer format to an LLGL format, or Format::Undefined if the format is opaque or a Y'CbCr format.
static Format AHardwareBufferFormatToLLGLFormat(std::uint32_t format)
{
    switch (format)
    {
        case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:     return Format::RGBA8UNorm;
        case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:     return Format::RGBA8UNorm;
        case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT: return Format::RGBA16Float;
        case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:  return Format::RGB10A2UNorm;
        default:                                        return Format::Undefined;
    }
}

bool AndroidGLQueryHardwareBufferProperties(AHardwareBuffer* buffer, ExternalImageProperties& outProperties)
{
    if (buffer == nullptr || !AndroidGLSupportsHardwareBuffers())
        return false;

    AHardwareBuffer_Desc bufferDesc = {};
    AHardwareBuffer_describe(buffer, &bufferDesc);

    /*
    GLES cannot configure the color model conversion; the driver derives it from the dataspace of the buffer.
    The Y'CbCr fields are therefore only informational, but the external format marks the buffer as opaque.
    */
    const Format format = AHardwareBufferFormatToLLGLFormat(bufferDesc.format);

    outProperties.extent                                = Extent3D{ bufferDesc.width, bufferDesc.height, 1u };
    outProperties.format                                = format;
    outProperties.requiresYcbcr                         = (format == Format::Undefined);
    outProperties.supportsLinearChromaFilter            = true;
    outProperties.ycbcrConversion                       = YcbcrConversionDescriptor{};
    outProperties.ycbcrConversion.format                = (format == Format::Undefined ? Format::Undefined : format);
    outProperties.ycbcrConversion.externalFormat        = (format == Format::Undefined ? static_cast<std::uint64_t>(bufferDesc.format) : 0u);
    outProperties.ycbcrConversion.chromaFilter          = SamplerFilter::Linear;

    return true;
}

void* AndroidGLAttachHardwareBufferToBoundTexture(AHardwareBuffer* buffer, GLint (&outExtent)[2])
{
    if (buffer == nullptr || !AndroidGLSupportsHardwareBuffers())
        return nullptr;

    EGLDisplay display = eglGetCurrentDisplay();

    /* Create EGLImage from native client buffer */
    EGLClientBuffer clientBuffer = g_eglGetNativeClientBufferANDROID(buffer);
    if (clientBuffer == nullptr)
    {
        Log::Errorf("eglGetNativeClientBufferANDROID failed: %s\n", EGLErrorToString());
        return nullptr;
    }

    const EGLint imageAttribs[] =
    {
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
    };
    EGLImageKHR image = g_eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, clientBuffer, imageAttribs);
    if (image == EGL_NO_IMAGE_KHR)
    {
        Log::Errorf("eglCreateImageKHR failed for Android hardware buffer: %s\n", EGLErrorToString());
        return nullptr;
    }

    /* Attach EGLImage to currently bound external texture */
    g_glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, static_cast<GLeglImageOES>(image));

    AHardwareBuffer_Desc bufferDesc = {};
    AHardwareBuffer_describe(buffer, &bufferDesc);
    outExtent[0] = static_cast<GLint>(bufferDesc.width);
    outExtent[1] = static_cast<GLint>(bufferDesc.height);

    return image;
}

void AndroidGLDestroyImage(void* image)
{
    if (image != nullptr && g_eglDestroyImageKHR != nullptr)
        g_eglDestroyImageKHR(eglGetCurrentDisplay(), static_cast<EGLImageKHR>(image));
}

void AndroidGLAcquireHardwareBuffer(AHardwareBuffer* buffer)
{
    if (buffer != nullptr)
        AHardwareBuffer_acquire(buffer);
}

void AndroidGLReleaseHardwareBuffer(AHardwareBuffer* buffer)
{
    if (buffer != nullptr)
        AHardwareBuffer_release(buffer);
}

void AndroidGLWaitNativeFence(int syncFd)
{
    if (syncFd < 0)
        return;

    LoadHardwareBufferProcsOnce();

    if (g_nativeFenceSyncSupported)
    {
        /* Import sync file descriptor into EGL sync object; EGL takes ownership of the descriptor on success */
        EGLDisplay display = eglGetCurrentDisplay();
        const EGLint syncAttribs[] =
        {
            EGL_SYNC_NATIVE_FENCE_FD_ANDROID, syncFd,
            EGL_NONE,
        };
        EGLSyncKHR sync = g_eglCreateSyncKHR(display, EGL_SYNC_NATIVE_FENCE_ANDROID, syncAttribs);
        if (sync != EGL_NO_SYNC_KHR)
        {
            /* Make the GL server wait for the fence without blocking the CPU */
            g_eglWaitSyncKHR(display, sync, 0);
            g_eglDestroySyncKHR(display, sync);
            return;
        }
    }

    /* Fall back to waiting on the CPU */
    struct pollfd pollFd;
    {
        pollFd.fd       = syncFd;
        pollFd.events   = POLLIN;
        pollFd.revents  = 0;
    }
    ::poll(&pollFd, 1, -1);
    ::close(syncFd);
}


} // /namespace LLGL



// ================================================================================
