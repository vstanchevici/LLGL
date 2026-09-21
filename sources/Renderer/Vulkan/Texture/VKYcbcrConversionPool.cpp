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
static VkChromaLocation GetSupportedVkChromaLocation(const ChromaLocation location, VkFormatFeatureFlags formatFeatures, bool hasKnownFormatFeatures)
{
    const VkChromaLocation requested = ToVkChromaLocation(location);
    if (!hasKnownFormatFeatures)
        return requested;

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
    VkFormatFeatureFlags                formatFeatures,
    bool                                hasKnownFormatFeatures)
:
    device_ { device                                                                        },
    desc_   { desc                                                                          },
    format_ { (desc.externalFormat != 0 ? VK_FORMAT_UNDEFINED : VKTypes::Map(desc.format))  }
{
    LLGL_ASSERT(vkCreateSamplerYcbcrConversionKHR != nullptr, "sampler Y'CbCr conversion not supported by Vulkan device");

    /*
    Fall back to nearest chroma filter if the format does not support linear filtering for chroma reconstruction.
    If the format features are unknown, i.e. an external format was not queried with RenderSystem::QueryExternalImageProperties,
    the nearest filter is used as well, since it is the only filter all formats are guaranteed to support.
    */
    chromaFilter_ = (desc.chromaFilter == SamplerFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    if (chromaFilter_ == VK_FILTER_LINEAR)
    {
        if (!hasKnownFormatFeatures)
        {
            Log::Printf(
                "Y'CbCr conversion: format features of external format are unknown; falling back to nearest chroma filter."
                " Use RenderSystem::QueryExternalImageProperties before creating the sampler to enable linear filtering\n"
            );
            chromaFilter_ = VK_FILTER_NEAREST;
        }
        else if ((formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) == 0)
        {
            Log::Printf("Y'CbCr conversion: linear chroma filter not supported by format; falling back to nearest\n");
            chromaFilter_ = VK_FILTER_NEAREST;
        }
    }

    /* Without known format features, conservatively assume min/mag filters must be equal to the chroma filter */
    hasSeparateReconstructionFilter_ =
    (
        hasKnownFormatFeatures &&
        (formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT) != 0
    );

    const bool forceExplicitReconstruction =
    (
        desc.forceExplicitReconstruction &&
        (!hasKnownFormatFeatures || (formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_FORCEABLE_BIT) != 0)
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
        createInfo.xChromaOffset                = GetSupportedVkChromaLocation(desc.xChromaOffset, formatFeatures, hasKnownFormatFeatures);
        createInfo.yChromaOffset                = GetSupportedVkChromaLocation(desc.yChromaOffset, formatFeatures, hasKnownFormatFeatures);
        createInfo.chromaFilter                 = chromaFilter_;
        createInfo.forceExplicitReconstruction  = VKBoolean(forceExplicitReconstruction);
    }

    #if VK_ANDROID_external_memory_android_hardware_buffer
    VkExternalFormatANDROID externalFormatInfo;
    if (desc.externalFormat != 0)
    {
        externalFormatInfo.sType            = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
        externalFormatInfo.pNext            = nullptr;
        externalFormatInfo.externalFormat   = desc.externalFormat;
        createInfo.pNext = &externalFormatInfo;
    }
    #else
    LLGL_ASSERT(desc.externalFormat == 0, "external formats for Y'CbCr conversions are only supported on Android");
    #endif

    VkResult result = vkCreateSamplerYcbcrConversionKHR(device, &createInfo, nullptr, &conversion_);
    VKThrowIfCreateFailed(result, "VkSamplerYcbcrConversion");

    CreateCanonicalSampler(hasKnownFormatFeatures && (formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0);
}

VKYcbcrConversion::~VKYcbcrConversion()
{
    if (canonicalSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, canonicalSampler_, nullptr);
    if (conversion_ != VK_NULL_HANDLE)
        vkDestroySamplerYcbcrConversionKHR(device_, conversion_, nullptr);
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

VKYcbcrConversionSPtr VKYcbcrConversionPool::Acquire(const YcbcrConversionDescriptor& desc)
{
    /* Remove expired entries and find an existing conversion with an identical descriptor */
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

    for (const std::weak_ptr<VKYcbcrConversion>& entry : conversions_)
    {
        if (VKYcbcrConversionSPtr conversion = entry.lock())
        {
            if (conversion->GetDesc() == desc)
                return conversion;
        }
    }

    /* Create new conversion */
    VkFormatFeatureFlags formatFeatures = 0;
    const bool hasKnownFormatFeatures = GetFormatFeatures(desc, formatFeatures);

    auto conversion = std::make_shared<VKYcbcrConversion>(device_, desc, formatFeatures, hasKnownFormatFeatures);
    conversions_.push_back(conversion);
    return conversion;
}

void VKYcbcrConversionPool::RegisterExternalFormatFeatures(std::uint64_t externalFormat, VkFormatFeatureFlags formatFeatures)
{
    if (externalFormat != 0)
        externalFormatFeatures_[externalFormat] = formatFeatures;
}

bool VKYcbcrConversionPool::GetFormatFeatures(const YcbcrConversionDescriptor& desc, VkFormatFeatureFlags& outFormatFeatures) const
{
    if (desc.externalFormat != 0)
    {
        /* External format features are only known if they have been registered before, e.g. by querying the external image */
        auto it = externalFormatFeatures_.find(desc.externalFormat);
        if (it != externalFormatFeatures_.end())
        {
            outFormatFeatures = it->second;
            return true;
        }
        return false;
    }
    else
    {
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, VKTypes::Map(desc.format), &formatProperties);
        outFormatFeatures = formatProperties.optimalTilingFeatures;
        return true;
    }
}


} // /namespace LLGL



// ================================================================================
