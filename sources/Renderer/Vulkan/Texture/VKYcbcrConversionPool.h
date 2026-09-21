/*
 * VKYcbcrConversionPool.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_YCBCR_CONVERSION_POOL_H
#define LLGL_VK_YCBCR_CONVERSION_POOL_H


#include "../Vulkan.h"
#include <LLGL/SamplerFlags.h>
#include <memory>
#include <vector>
#include <cstdint>


namespace LLGL
{


// Wrapper for a native VkSamplerYcbcrConversion object. Instances are shared between samplers and textures with an identical conversion.
class VKYcbcrConversion
{

    public:

        VKYcbcrConversion(
            VkDevice                            device,
            const YcbcrConversionDescriptor&    desc,
            VkFormatFeatureFlags                formatFeatures
        );

        /*
        Wraps a native conversion and a sampler that was created with it by the client (see Resource::SetNativeHandle).
        The native objects are only destroyed together with this object if 'own' is true.
        */
        VKYcbcrConversion(
            VkDevice                            device,
            VkSamplerYcbcrConversion            nativeConversion,
            VkSampler                           nativeSampler,
            VkFormat                            format,
            bool                                own
        );

        ~VKYcbcrConversion();

        VKYcbcrConversion(const VKYcbcrConversion&) = delete;
        VKYcbcrConversion& operator = (const VKYcbcrConversion&) = delete;

        // Returns the native Vulkan sampler Y'CbCr conversion object.
        inline VkSamplerYcbcrConversion GetVkSamplerYcbcrConversion() const
        {
            return conversion_;
        }

        // Returns the descriptor this conversion was requested with. This is used as key to share conversions.
        inline const YcbcrConversionDescriptor& GetDesc() const
        {
            return desc_;
        }

        // Returns the native multi-planar format of this conversion.
        inline VkFormat GetVkFormat() const
        {
            return format_;
        }

        // Returns the chroma filter this conversion was created with (after applying fallbacks for unsupported format features).
        inline VkFilter GetChromaFilter() const
        {
            return chromaFilter_;
        }

        // Returns true if samplers with this conversion can have min/mag filters that differ from the chroma filter.
        inline bool HasSeparateReconstructionFilter() const
        {
            return hasSeparateReconstructionFilter_;
        }

        /*
        Returns the canonical sampler of this conversion. All Y'CbCr samplers are equivalent for a given conversion
        (clamp-to-edge, no MIP-maps or anisotropy), so this sampler is used as immutable sampler for all combined texture-samplers with this conversion.
        */
        inline VkSampler GetCanonicalVkSampler() const
        {
            return canonicalSampler_;
        }

        // Returns the address of the canonical sampler, e.g. for VkDescriptorSetLayoutBinding::pImmutableSamplers. It remains valid for the lifetime of this conversion.
        inline const VkSampler* GetCanonicalVkSamplerAddress() const
        {
            return &canonicalSampler_;
        }

        // Returns true if this conversion wraps native objects that were created by the client.
        inline bool IsNative() const
        {
            return isNative_;
        }

        // Takes ownership of the native objects of this conversion, i.e. they will be destroyed together with this object.
        inline void TakeOwnership()
        {
            ownsNativeObjects_ = true;
        }

    private:

        void CreateCanonicalSampler(bool supportsLinearFilter);

    private:

        VkDevice                    device_                             = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion    conversion_                         = VK_NULL_HANDLE;
        VkSampler                   canonicalSampler_                   = VK_NULL_HANDLE;
        YcbcrConversionDescriptor   desc_;
        VkFormat                    format_                             = VK_FORMAT_UNDEFINED;
        VkFilter                    chromaFilter_                       = VK_FILTER_NEAREST;
        bool                        hasSeparateReconstructionFilter_    = false;
        bool                        isNative_                           = false;
        bool                        ownsNativeObjects_                  = true;

};

using VKYcbcrConversionSPtr = std::shared_ptr<VKYcbcrConversion>;

// Returns true if the specified descriptor requires a native Y'CbCr conversion, i.e. it has a multi-planar format.
inline bool IsYcbcrConversionRequired(const YcbcrConversionDescriptor& desc)
{
    return IsMultiPlanarFormat(desc.format);
}

// Pool of sampler Y'CbCr conversions. Samplers and textures with an identical conversion descriptor share the same native object.
class VKYcbcrConversionPool
{

    public:

        VKYcbcrConversionPool(VkDevice device, VkPhysicalDevice physicalDevice);

        // Returns a shared conversion for the specified descriptor. The native object is destroyed when the last reference is released.
        VKYcbcrConversionSPtr Acquire(const YcbcrConversionDescriptor& desc);

        /*
        Returns a shared wrapper for the specified native conversion and sampler that were created by the client.
        Textures with the same native handles share the same wrapper and thus the same pipeline variants.
        If 'own' is true, the native objects are destroyed when the last reference is released.
        */
        VKYcbcrConversionSPtr AcquireNative(VkSamplerYcbcrConversion nativeConversion, VkSampler nativeSampler, VkFormat format, bool own);

    private:

        void RemoveExpiredEntries();

    private:

        VkDevice                                        device_             = VK_NULL_HANDLE;
        VkPhysicalDevice                                physicalDevice_     = VK_NULL_HANDLE;
        std::vector<std::weak_ptr<VKYcbcrConversion>>   conversions_;

};


} // /namespace LLGL


#endif



// ================================================================================
