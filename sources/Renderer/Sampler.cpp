/*
 * Sampler.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include <LLGL/Sampler.h>


namespace LLGL
{


ResourceType Sampler::GetResourceType() const
{
    return ResourceType::Sampler;
}

LLGL_EXPORT bool operator == (const YcbcrConversionDescriptor& lhs, const YcbcrConversionDescriptor& rhs)
{
    return
    (
        lhs.format                      == rhs.format                       &&
        lhs.externalFormat              == rhs.externalFormat               &&
        lhs.model                       == rhs.model                        &&
        lhs.range                       == rhs.range                        &&
        lhs.xChromaOffset               == rhs.xChromaOffset                &&
        lhs.yChromaOffset               == rhs.yChromaOffset                &&
        lhs.chromaFilter                == rhs.chromaFilter                 &&
        lhs.swizzle.r                   == rhs.swizzle.r                    &&
        lhs.swizzle.g                   == rhs.swizzle.g                    &&
        lhs.swizzle.b                   == rhs.swizzle.b                    &&
        lhs.swizzle.a                   == rhs.swizzle.a                    &&
        lhs.forceExplicitReconstruction == rhs.forceExplicitReconstruction
    );
}

LLGL_EXPORT bool operator != (const YcbcrConversionDescriptor& lhs, const YcbcrConversionDescriptor& rhs)
{
    return !(lhs == rhs);
}


} // /namespace LLGL



// ================================================================================
