/*
 * VKYcbcrConversionPool.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKYcbcrConversionPool.h"
#include "../Ext/VKExtensions.h"
#include "../VKTypes.h"
#include "../VKCore.h"
#include "../../../Core/Assertion.h"
#include <LLGL/Log.h>
#include <algorithm>


namespace LLGL
{


/*
 * VKYcbcrConversion class
 */

static VkSamplerYcbcrModelConversion ToVkSamplerYcbcrModelConversion(const YcbcrModel model)
{
    switch (model)
    {
        case YcbcrModel::RGBIdentity:   return VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
        case YcbcrModel::YcbcrIdentity: return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY;
        case YcbcrModel::Ycbcr709:      return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
        case YcbcrModel::Ycbcr601:      return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
        case YcbcrModel::Ycbcr2020:     return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
    }
    VKTypes::MapFailed("YcbcrModel", "VkSamplerYcbcrModelConversion");
}

static VkSamplerYcbcrRange ToVkSamplerYcbcrRange(const YcbcrRange range)
{
    switch (range)
    {
        case YcbcrRange::Full:      return VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
        case YcbcrRange::Narrow:    return VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
    }
    VKTypes::MapFailed("YcbcrRange", "VkSamplerYcbcrRange");
}

static VkChromaLocation ToVkChromaLocation(const ChromaLocation location)
{
    switch (location)
    {
        case ChromaLocation::CositedEven:   return VK_CHROMA_LOCATION_COSITED_EVEN;
        case ChromaLocation::Midpoint:      return VK_CHROMA_LOCATION_MIDPOINT;
    }
    VKTypes::MapFailed("ChromaLocation", "VkChromaLocation");
}

// Returns a chroma location that is supported by the specified format features, preferring the requested location.
static VkChromaLocation GetSupportedVkChromaLocation(const ChromaLocation location, VkFormatFeatureFlags formatFeatures)
{
    const VkChromaLocation requested = ToVkChromaLocation(location);

    const bool supportsMidpoint = ((formatFeatures & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) != 0);
    const bool supportsCosited  = ((formatFeatures & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT ) != 0);

    if (requested == VK_CHROMA_LOCATION_MIDPOINT && !supportsMidpoint && supportsCosited)
    {
        Log::Printf("Y'CbCr conversion: midpoint chroma location not supported by format; falling back to cosited-even\n");
        return VK_CHROMA_LOCATION_COSITED_EVEN;
    }
    if (requested == VK_CHROMA_LOCATION_COSITED_EVEN && !supportsCosited && supportsMidpoint)
    {
        Log::Printf("Y'CbCr conversion: cosited-even chroma location not supported by format; falling back to midpoint\n");
        return VK_CHROMA_LOCATION_MIDPOINT;
    }

    return requested;
}

VKYcbcrConversion::VKYcbcrConversion(
    VkDevice                            device,
    const YcbcrConversionDescriptor&    desc,
    VkFormatFeatureFlags                formatFeatures)
:
    device_ { device                    },
    desc_   { desc                      },
    format_ { VKTypes::Map(desc.format) }
{
    LLGL_ASSERT(vkCreateSamplerYcbcrConversionKHR != nullptr, "sampler Y'CbCr conversion not supported by Vulkan device");

    /* Fall back to nearest chroma filter if the format does not support linear filtering for chroma reconstruction */
    chromaFilter_ = (desc.chromaFilter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    if (chromaFilter_ == VK_FILTER_LINEAR && (formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) == 0)
    {
        Log::Printf("Y'CbCr conversion: linear chroma filter not supported by format; falling back to nearest\n");
        chromaFilter_ = VK_FILTER_NEAREST;
    }

    hasSeparateReconstructionFilter_ = ((formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT) != 0);

    const bool forceExplicitReconstruction =
    (
        desc.forceExplicitReconstruction &&
        (formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_FORCEABLE_BIT) != 0
    );

    /* Create native sampler Y'CbCr conversion */
    VkSamplerYcbcrConversionCreateInfo createInfo;
    {
        createInfo.sType                        = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        createInfo.pNext                        = nullptr;
        createInfo.format                       = format_;
        createInfo.ycbcrModel                   = ToVkSamplerYcbcrModelConversion(desc.model);
        createInfo.ycbcrRange                   = ToVkSamplerYcbcrRange(desc.range);
        createInfo.components.r                 = VKTypes::ToVkComponentSwizzle(desc.swizzle.r);
        createInfo.components.g                 = VKTypes::ToVkComponentSwizzle(desc.swizzle.g);
        createInfo.components.b                 = VKTypes::ToVkComponentSwizzle(desc.swizzle.b);
        createInfo.components.a                 = VKTypes::ToVkComponentSwizzle(desc.swizzle.a);
        createInfo.xChromaOffset                = GetSupportedVkChromaLocation(desc.xChromaOffset, formatFeatures);
        createInfo.yChromaOffset                = GetSupportedVkChromaLocation(desc.yChromaOffset, formatFeatures);
        createInfo.chromaFilter                 = chromaFilter_;
        createInfo.forceExplicitReconstruction  = VKBoolean(forceExplicitReconstruction);
    }

    VkResult result = vkCreateSamplerYcbcrConversionKHR(device, &createInfo, nullptr, &conversion_);
    VKThrowIfCreateFailed(result, "VkSamplerYcbcrConversion");

    CreateCanonicalSampler((formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0);
}

VKYcbcrConversion::VKYcbcrConversion(
    VkDevice                    device,
    VkSamplerYcbcrConversion    nativeConversion,
    VkSampler                   nativeSampler,
    VkFormat                    format,
    bool                        own)
:
    device_             { device           },
    conversion_         { nativeConversion },
    canonicalSampler_   { nativeSampler    },
    format_             { format           },
    isNative_           { true             },
    ownsNativeObjects_  { own              }
{
}

VKYcbcrConversion::~VKYcbcrConversion()
{
    if (!ownsNativeObjects_)
        return;

    if (canonicalSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, canonicalSampler_, nullptr);

    if (conversion_ != VK_NULL_HANDLE)
    {
        if (vkDestroySamplerYcbcrConversionKHR != nullptr)
            vkDestroySamplerYcbcrConversionKHR(device_, conversion_, nullptr);
        else
            Log::Errorf("cannot destroy VkSamplerYcbcrConversion, since the samplerYcbcrConversion feature is not enabled for this device\n");
    }
}

void VKYcbcrConversion::CreateCanonicalSampler(bool supportsLinearFilter)
{
    /* Chain Y'CbCr conversion into sampler */
    VkSamplerYcbcrConversionInfo conversionInfo;
    {
        conversionInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        conversionInfo.pNext        = nullptr;
        conversionInfo.conversion   = conversion_;
    }

    /*
    Min/mag filters must equal the chroma filter unless the format supports separate reconstruction filters.
    All other attributes are restricted for samplers with Y'CbCr conversion:
    see https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
    */
    const VkFilter filter = (hasSeparateReconstructionFilter_ && supportsLinearFilter ? VK_FILTER_LINEAR : chromaFilter_);

    VkSamplerCreateInfo createInfo;
    {
        createInfo.sType                    = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.pNext                    = &conversionInfo;
        createInfo.flags                    = 0;
        createInfo.magFilter                = filter;
        createInfo.minFilter                = filter;
        createInfo.mipmapMode               = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeV             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.addressModeW             = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createInfo.mipLodBias               = 0.0f;
        createInfo.anisotropyEnable         = VK_FALSE;
        createInfo.maxAnisotropy            = 1.0f;
        createInfo.compareEnable            = VK_FALSE;
        createInfo.compareOp                = VK_COMPARE_OP_NEVER;
        createInfo.minLod                   = 0.0f;
        createInfo.maxLod                   = 0.0f;
        createInfo.borderColor              = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        createInfo.unnormalizedCoordinates  = VK_FALSE;
    }
    VkResult result = vkCreateSampler(device_, &createInfo, nullptr, &canonicalSampler_);
    VKThrowIfCreateFailed(result, "VkSampler");
}


/*
 * VKYcbcrConversionPool class
 */

VKYcbcrConversionPool::VKYcbcrConversionPool(VkDevice device, VkPhysicalDevice physicalDevice) :
    device_         { device         },
    physicalDevice_ { physicalDevice }
{
}

void VKYcbcrConversionPool::RemoveExpiredEntries()
{
    conversions_.erase(
        std::remove_if(
            conversions_.begin(),
            conversions_.end(),
            [](const std::weak_ptr<VKYcbcrConversion>& entry) -> bool
            {
                return entry.expired();
            }
        ),
        conversions_.end()
    );
}

VKYcbcrConversionSPtr VKYcbcrConversionPool::Acquire(const YcbcrConversionDescriptor& desc)
{
    /* Remove expired entries and find an existing conversion with an identical descriptor */
    RemoveExpiredEntries();

    for (const std::weak_ptr<VKYcbcrConversion>& entry : conversions_)
    {
        if (VKYcbcrConversionSPtr conversion = entry.lock())
        {
            if (!conversion->IsNative() && conversion->GetDesc() == desc)
                return conversion;
        }
    }

    /* Create new conversion with the features of its format */
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, VKTypes::Map(desc.format), &formatProperties);

    auto conversion = std::make_shared<VKYcbcrConversion>(device_, desc, formatProperties.optimalTilingFeatures);
    conversions_.push_back(conversion);
    return conversion;
}

VKYcbcrConversionSPtr VKYcbcrConversionPool::AcquireNative(VkSamplerYcbcrConversion nativeConversion, VkSampler nativeSampler, VkFormat format, bool own)
{
    /* Remove expired entries and find an existing wrapper for the same native objects */
    RemoveExpiredEntries();

    for (const std::weak_ptr<VKYcbcrConversion>& entry : conversions_)
    {
        if (VKYcbcrConversionSPtr conversion = entry.lock())
        {
            if (conversion->IsNative() &&
                conversion->GetVkSamplerYcbcrConversion() == nativeConversion &&
                conversion->GetCanonicalVkSampler() == nativeSampler)
            {
                if (own)
                    conversion->TakeOwnership();
                return conversion;
            }
        }
    }

    /* Create new wrapper for native objects */
    auto conversion = std::make_shared<VKYcbcrConversion>(device_, nativeConversion, nativeSampler, format, own);
    conversions_.push_back(conversion);
    return conversion;
}


} // /namespace LLGL



// ================================================================================
