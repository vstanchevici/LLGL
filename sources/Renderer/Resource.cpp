/*
 * Resource.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include <LLGL/Resource.h>


namespace LLGL
{


bool Resource::SetNativeHandle(void* /*nativeHandle*/, std::size_t /*nativeHandleSize*/, bool /*own*/)
{
    return false; // Not supported by default
}


} // /namespace LLGL



// ================================================================================
