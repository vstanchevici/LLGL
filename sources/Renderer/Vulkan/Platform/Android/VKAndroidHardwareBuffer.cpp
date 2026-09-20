/*
 * VKAndroidHardwareBuffer.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKAndroidHardwareBuffer.h"
#include "../../Ext/VKExtensions.h"
#include "../../VKTypes.h"
#include "../../VKCore.h"
#include "../../../../Core/Assertion.h"
#include <LLGL/RenderSystemFlags.h>
#include <android/hardware_buffer.h>


namespace LLGL
{


bool VKQueryAndroidHardwareBufferProperties(VkDevice device, AHardwareBuffer* buffer, VKAndroidHardwareBufferProperties& outProperties)
{
    if (buffer == nullptr || vkGetAndroidHardwareBufferPropertiesANDROID == nullptr)
        return false;

    /* Query memory and format properties of hardware buffer */
    VkAndroidHardwareBufferFormatPropertiesANDROID formatProps = {};
    formatProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

    VkAndroidHardwareBufferPropertiesANDROID bufferProps = {};
    bufferProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    bufferProps.pNext = &formatProps;

    VkResult result = vkGetAndroidHardwareBufferPropertiesANDROID(device, buffer, &bufferProps);
    if (result != VK_SUCCESS)
        return false;

    /* Query dimensions of hardware buffer */
    AHardwareBuffer_Desc bufferDesc = {};
    AHardwareBuffer_describe(buffer, &bufferDesc);

    outProperties.allocationSize            = bufferProps.allocationSize;
    outProperties.memoryTypeBits            = bufferProps.memoryTypeBits;
    outProperties.format                    = formatProps.format;
    outProperties.externalFormat            = formatProps.externalFormat;
    outProperties.formatFeatures            = formatProps.formatFeatures;
    outProperties.components                = formatProps.samplerYcbcrConversionComponents;
    outProperties.suggestedYcbcrModel       = formatProps.suggestedYcbcrModel;
    outProperties.suggestedYcbcrRange       = formatProps.suggestedYcbcrRange;
    outProperties.suggestedXChromaOffset    = formatProps.suggestedXChromaOffset;
    outProperties.suggestedYChromaOffset    = formatProps.suggestedYChromaOffset;
    outProperties.extent                    = VkExtent3D{ bufferDesc.width, bufferDesc.height, 1u };

    return true;
}

static TextureSwizzle ToTextureSwizzle(VkComponentSwizzle swizzle, TextureSwizzle identity)
{
    switch (swizzle)
    {
        case VK_COMPONENT_SWIZZLE_IDENTITY: return identity;
        case VK_COMPONENT_SWIZZLE_ZERO:     return TextureSwizzle::Zero;
        case VK_COMPONENT_SWIZZLE_ONE:      return TextureSwizzle::One;
        case VK_COMPONENT_SWIZZLE_R:        return TextureSwizzle::Red;
        case VK_COMPONENT_SWIZZLE_G:        return TextureSwizzle::Green;
        case VK_COMPONENT_SWIZZLE_B:        return TextureSwizzle::Blue;
        case VK_COMPONENT_SWIZZLE_A:        return TextureSwizzle::Alpha;
        default:                            return identity;
    }
}

static YcbcrModel ToYcbcrModel(VkSamplerYcbcrModelConversion model)
{
    switch (model)
    {
        case VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY:    return YcbcrModel::RGBIdentity;
        case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY:  return YcbcrModel::YcbcrIdentity;
        case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709:       return YcbcrModel::Ycbcr709;
        case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601:       return YcbcrModel::Ycbcr601;
        case VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020:      return YcbcrModel::Ycbcr2020;
        default:                                                return YcbcrModel::Ycbcr709;
    }
}

static YcbcrRange ToYcbcrRange(VkSamplerYcbcrRange range)
{
    return (range == VK_SAMPLER_YCBCR_RANGE_ITU_FULL ? YcbcrRange::Full : YcbcrRange::Narrow);
}

static ChromaLocation ToChromaLocation(VkChromaLocation location)
{
    return (location == VK_CHROMA_LOCATION_MIDPOINT ? ChromaLocation::Midpoint : ChromaLocation::CositedEven);
}

void VKConvertAndroidHardwareBufferProperties(const VKAndroidHardwareBufferProperties& props, ExternalImageProperties& outProperties)
{
    /* Prefer the native format if LLGL knows it, otherwise the image can only be sampled with the opaque external format */
    const Format format = VKTypes::Unmap(props.format);
    const bool useExternalFormat = (format == Format::Undefined);

    const bool supportsLinearChromaFilter = ((props.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) != 0);

    outProperties.extent                            = Extent3D{ props.extent.width, props.extent.height, 1u };
    outProperties.format                            = format;
    outProperties.requiresYcbcr                     = (useExternalFormat || IsMultiPlanarFormat(format));
    outProperties.supportsLinearChromaFilter        = supportsLinearChromaFilter;

    YcbcrConversionDescriptor& ycbcrDesc = outProperties.ycbcrConversion;
    {
        ycbcrDesc.format                        = (useExternalFormat ? Format::Undefined : format);
        ycbcrDesc.externalFormat                = (useExternalFormat ? props.externalFormat : 0);
        ycbcrDesc.model                         = ToYcbcrModel(props.suggestedYcbcrModel);
        ycbcrDesc.range                         = ToYcbcrRange(props.suggestedYcbcrRange);
        ycbcrDesc.xChromaOffset                 = ToChromaLocation(props.suggestedXChromaOffset);
        ycbcrDesc.yChromaOffset                 = ToChromaLocation(props.suggestedYChromaOffset);
        ycbcrDesc.chromaFilter                  = (supportsLinearChromaFilter ? SamplerFilter::Linear : SamplerFilter::Nearest);
        ycbcrDesc.swizzle.r                     = ToTextureSwizzle(props.components.r, TextureSwizzle::Red  );
        ycbcrDesc.swizzle.g                     = ToTextureSwizzle(props.components.g, TextureSwizzle::Green);
        ycbcrDesc.swizzle.b                     = ToTextureSwizzle(props.components.b, TextureSwizzle::Blue );
        ycbcrDesc.swizzle.a                     = ToTextureSwizzle(props.components.a, TextureSwizzle::Alpha);
        ycbcrDesc.forceExplicitReconstruction   = false;
    }
}

// Returns the index of the lowest bit that is set in the specified bitmask.
static std::uint32_t FindLowestMemoryTypeIndex(std::uint32_t memoryTypeBits)
{
    for (std::uint32_t i = 0; i < 32; ++i)
    {
        if ((memoryTypeBits & (1u << i)) != 0)
            return i;
    }
    return 0;
}

void VKImportAndroidHardwareBufferMemory(
    VkDevice                                    device,
    AHardwareBuffer*                            buffer,
    const VKAndroidHardwareBufferProperties&    props,
    VkImage                                     image,
    VKPtr<VkDeviceMemory>&                      outMemory)
{
    /* Images that are backed by an Android hardware buffer require a dedicated allocation */
    VkMemoryDedicatedAllocateInfo dedicatedAllocInfo;
    {
        dedicatedAllocInfo.sType            = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicatedAllocInfo.pNext            = nullptr;
        dedicatedAllocInfo.image            = image;
        dedicatedAllocInfo.buffer           = VK_NULL_HANDLE;
    }
    VkImportAndroidHardwareBufferInfoANDROID importInfo;
    {
        importInfo.sType                    = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
        importInfo.pNext                    = &dedicatedAllocInfo;
        importInfo.buffer                   = buffer;
    }
    VkMemoryAllocateInfo allocInfo;
    {
        allocInfo.sType                     = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext                     = &importInfo;
        allocInfo.allocationSize            = props.allocationSize;
        allocInfo.memoryTypeIndex           = FindLowestMemoryTypeIndex(props.memoryTypeBits);
    }
    VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, outMemory.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to import memory of Android hardware buffer");

    result = vkBindImageMemory(device, image, outMemory.Get(), 0);
    VKThrowIfFailed(result, "failed to bind memory of Android hardware buffer to Vulkan image");
}

void VKAcquireAndroidHardwareBuffer(AHardwareBuffer* buffer)
{
    if (buffer != nullptr)
        AHardwareBuffer_acquire(buffer);
}

void VKReleaseAndroidHardwareBuffer(AHardwareBuffer* buffer)
{
    if (buffer != nullptr)
        AHardwareBuffer_release(buffer);
}


} // /namespace LLGL



// ================================================================================
