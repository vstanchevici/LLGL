/*
 * VKSampler.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_SAMPLER_H
#define LLGL_VK_SAMPLER_H


#include <LLGL/Sampler.h>
#include "VKYcbcrConversionPool.h"
#include "../Vulkan.h"
#include "../VKPtr.h"


namespace LLGL
{


class VKSampler final : public Sampler
{

    public:

        #include <LLGL/Backend/Sampler.inl>

    public:

        void SetDebugName(const char* name) override;

        bool SetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize, bool own = false) override;

    public:

        // Creates a sampler. If SamplerDescriptor::ycbcrConversion is non-null, the conversion is acquired from the specified pool.
        VKSampler(VkDevice device, const SamplerDescriptor& desc, VKYcbcrConversionPool* ycbcrConversionPool = nullptr);

        // Returns the Vulkan sampler object. For samplers with Y'CbCr conversion, this is the canonical sampler of the conversion.
        inline VkSampler GetVkSampler() const
        {
            return (ycbcrConversion_ ? ycbcrConversion_->GetCanonicalVkSampler() : sampler_.Get());
        }

        // Returns the Y'CbCr conversion of this sampler or null if this sampler has no conversion.
        inline VKYcbcrConversion* GetYcbcrConversion() const
        {
            return ycbcrConversion_.get();
        }

    public:

        // Converts the specified sampler descriptor to the native Vulkan descriptor.
        static void ConvertDesc(VkSamplerCreateInfo& outDesc, const SamplerDescriptor& inDesc);

        // Creates a native Vulkan sampler.
        static VKPtr<VkSampler> CreateVkSampler(VkDevice device, const SamplerDescriptor& desc);

    private:

        VkDevice                device_             = VK_NULL_HANDLE;
        VKYcbcrConversionPool*  ycbcrConversionPool_ = nullptr;
        VKYcbcrConversionSPtr   ycbcrConversion_;
        VKPtr<VkSampler>        sampler_;           // Null for samplers with Y'CbCr conversion

};


} // /namespace LLGL


#endif



// ================================================================================
