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
#include <map>
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
            VkFormatFeatureFlags                formatFeatures,
            bool                                hasKnownFormatFeatures
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

        // Returns the native format of this conversion. This is VK_FORMAT_UNDEFINED for external formats.
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

    private:

        VkDevice                    device_                             = VK_NULL_HANDLE;
        VkSamplerYcbcrConversion    conversion_                         = VK_NULL_HANDLE;
        YcbcrConversionDescriptor   desc_;
        VkFormat                    format_                             = VK_FORMAT_UNDEFINED;
        VkFilter                    chromaFilter_                       = VK_FILTER_NEAREST;
        bool                        hasSeparateReconstructionFilter_    = false;

};

using VKYcbcrConversionSPtr = std::shared_ptr<VKYcbcrConversion>;

// Returns true if the specified descriptor requires a native Y'CbCr conversion, i.e. it has a multi-planar or external format.
// Descriptors for single-planar formats (e.g. of RGBA external images) are ignored, so they can be passed through by the client.
inline bool IsYcbcrConversionRequired(const YcbcrConversionDescriptor& desc)
{
    return (desc.externalFormat != 0 || IsMultiPlanarFormat(desc.format));
}

// Pool of sampler Y'CbCr conversions. Samplers and textures with an identical conversion descriptor share the same native object.
class VKYcbcrConversionPool
{

    public:

        VKYcbcrConversionPool(VkDevice device, VkPhysicalDevice physicalDevice);

        // Returns a shared conversion for the specified descriptor. The native object is destroyed when the last reference is released.
        VKYcbcrConversionSPtr Acquire(const YcbcrConversionDescriptor& desc);

        // Stores the format features of an external format, e.g. from VkAndroidHardwareBufferFormatPropertiesANDROID::formatFeatures.
        void RegisterExternalFormatFeatures(std::uint64_t externalFormat, VkFormatFeatureFlags formatFeatures);

        // Returns the format features for the specified conversion descriptor. Returns false if they are unknown.
        bool GetFormatFeatures(const YcbcrConversionDescriptor& desc, VkFormatFeatureFlags& outFormatFeatures) const;

    private:

        VkDevice                                        device_             = VK_NULL_HANDLE;
        VkPhysicalDevice                                physicalDevice_     = VK_NULL_HANDLE;
        std::vector<std::weak_ptr<VKYcbcrConversion>>   conversions_;
        std::map<std::uint64_t, VkFormatFeatureFlags>   externalFormatFeatures_;

};


} // /namespace LLGL


#endif



// ================================================================================
