/*
 * AndroidGLHardwareBuffer.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_ANDROID_GL_HARDWARE_BUFFER_H
#define LLGL_ANDROID_GL_HARDWARE_BUFFER_H


#include "../../OpenGL.h"
#include <cstdint>


struct AHardwareBuffer;

namespace LLGL
{


struct ExternalImageProperties;

/*
Returns true if Android hardware buffers can be imported as GL_TEXTURE_EXTERNAL_OES textures.
This requires GL_OES_EGL_image_external_essl3, EGL_KHR_image_base, EGL_ANDROID_get_native_client_buffer, and EGL_ANDROID_image_native_buffer.
The EGL and GLES procedures are loaded on the first call; a GL context must be current.
*/
bool AndroidGLSupportsHardwareBuffers();

// Queries the properties of the specified Android hardware buffer. Returns false if the buffer cannot be imported.
bool AndroidGLQueryHardwareBufferProperties(AHardwareBuffer* buffer, ExternalImageProperties& outProperties);

/*
Creates an EGLImage for the specified Android hardware buffer and attaches it to the texture that is currently bound to GL_TEXTURE_EXTERNAL_OES.
Returns the EGLImage handle or null on failure. The dimensions of the buffer are written to 'outExtent'.
*/
void* AndroidGLAttachHardwareBufferToBoundTexture(AHardwareBuffer* buffer, GLint (&outExtent)[2]);

// Destroys the specified EGLImage that was created by AndroidGLAttachHardwareBufferToBoundTexture.
void AndroidGLDestroyImage(void* image);

// Acquires a reference to the specified Android hardware buffer.
void AndroidGLAcquireHardwareBuffer(AHardwareBuffer* buffer);

// Releases a reference to the specified Android hardware buffer.
void AndroidGLReleaseHardwareBuffer(AHardwareBuffer* buffer);


} // /namespace LLGL


#endif



// ================================================================================
