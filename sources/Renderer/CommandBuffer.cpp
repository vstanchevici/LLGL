/*
 * CommandBuffer.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include <LLGL/CommandBuffer.h>
#include <LLGL/Platform/Platform.h>

#if defined LLGL_OS_ANDROID || defined LLGL_OS_LINUX || defined LLGL_OS_MACOS || defined LLGL_OS_IOS
#   include <unistd.h>
#endif


namespace LLGL
{


void CommandBuffer::AcquireExternalTexture(Texture& /*texture*/, long long nativeFence)
{
    /* External textures are not supported by default, but ownership of the fence must still be honored */
    #if defined LLGL_OS_ANDROID || defined LLGL_OS_LINUX || defined LLGL_OS_MACOS || defined LLGL_OS_IOS
    if (nativeFence >= 0)
        ::close(static_cast<int>(nativeFence));
    #else
    (void)nativeFence;
    #endif
}

void CommandBuffer::ReleaseExternalTexture(Texture& /*texture*/)
{
    // dummy
}


} // /namespace LLGL



// ================================================================================
