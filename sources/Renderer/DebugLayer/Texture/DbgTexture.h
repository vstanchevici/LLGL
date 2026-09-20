/*
 * DbgTexture.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_DBG_TEXTURE_H
#define LLGL_DBG_TEXTURE_H


#include <LLGL/Texture.h>
#include <LLGL/SamplerFlags.h>
#include <string>
#include <set>


namespace LLGL
{


class DbgTexture final : public Texture
{

    public:

        #include <LLGL/Backend/Texture.inl>

    public:

        void SetDebugName(const char* name) override;

    public:

        DbgTexture(Texture& instance, const TextureDescriptor& desc);
        //DbgTexture(Texture& instance, DbgTexture* sharedTexture, const TextureViewDescriptor& desc);
        ~DbgTexture();

    public:

        Texture&                instance;
        const TextureDescriptor     desc;                           // Copy of the descriptor without the external and ycbcrConversion pointers.
        TextureViewDescriptor       viewDesc;
        std::uint32_t               mipLevels           = 1;        // Actual number of MIP-map levels.
        std::string                 label;
        const bool                  isTextureView       = false;
        const bool                  isExternal          = false;    // Texture was created with TextureDescriptor::external.
        const bool                  hasYcbcrConversion  = false;    // Texture was created with TextureDescriptor::ycbcrConversion.
        YcbcrConversionDescriptor   ycbcrConversion;                // Copy of TextureDescriptor::ycbcrConversion if hasYcbcrConversion is true.

    private:

        //DbgTexture*             sharedTexture_      = nullptr;  // Reference to the shared texture (only for texture-views)
        //std::set<DbgTexture*>   sharedTextureViews_;             // List of texture views that share the image data with this texture

};


} // /namespace LLGL


#endif



// ================================================================================
