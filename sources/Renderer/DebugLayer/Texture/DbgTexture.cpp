/*
 * DbgTexture.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "DbgTexture.h"
#include "../DbgCore.h"


namespace LLGL
{


// Returns a copy of the specified texture descriptor without pointers that are only valid during the call to RenderSystem::CreateTexture.
static TextureDescriptor GetDbgTextureDescWithoutTransientPointers(const TextureDescriptor& desc)
{
    TextureDescriptor descCopy = desc;
    descCopy.ycbcrConversion = nullptr;
    return descCopy;
}

DbgTexture::DbgTexture(Texture& instance, const TextureDescriptor& desc) :
    Texture            { desc.type, desc.bindFlags                          },
    instance           { instance                                           },
    desc               { GetDbgTextureDescWithoutTransientPointers(desc)    },
    mipLevels          { NumMipLevels(desc)                                 },
    label              { LLGL_DBG_LABEL(desc)                               },
    hasYcbcrConversion { (desc.ycbcrConversion != nullptr)                  }
{
    if (desc.ycbcrConversion != nullptr)
        ycbcrConversion = *desc.ycbcrConversion;
}

#if 0
DbgTexture::DbgTexture(Texture& instance, DbgTexture* sharedTexture, const TextureViewDescriptor& desc) :
    Texture        { desc.type, sharedTexture->desc.bindFlags },
    instance       { instance                                 },
    viewDesc       { desc                                     },
    mipLevels      { desc.subresource.numMipLevels            },
    isTextureView  { true                                     },
    sharedTexture_ { sharedTexture                            }
{
    sharedTexture->sharedTextureViews_.insert(this);
}
#endif

DbgTexture::~DbgTexture()
{
    #if 0
    /* Remove references between shared texture and texture views */
    if (sharedTexture_)
        sharedTexture_->sharedTextureViews_.erase(this);
    for (auto textureView : sharedTextureViews_)
        textureView->sharedTexture_ = nullptr;
    #endif
}

bool DbgTexture::GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize)
{
    return instance.GetNativeHandle(nativeHandle, nativeHandleSize);
}

bool DbgTexture::SetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize, bool own)
{
    if (!instance.SetNativeHandle(nativeHandle, nativeHandleSize, own))
        return false;
    hasNativeHandle = true;
    return true;
}

void DbgTexture::SetDebugName(const char* name)
{
    DbgSetObjectName(*this, name);
}

TextureDescriptor DbgTexture::GetDesc() const
{
    return instance.GetDesc();
}

Format DbgTexture::GetFormat() const
{
    return instance.GetFormat();
}

Extent3D DbgTexture::GetMipExtent(std::uint32_t mipLevel) const
{
    return instance.GetMipExtent(mipLevel);
}

SubresourceFootprint DbgTexture::GetSubresourceFootprint(std::uint32_t mipLevel) const
{
    return instance.GetSubresourceFootprint(mipLevel);
}


} // /namespace LLGL



// ================================================================================
