/*
 * VKTexture.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKTexture.h"
#include "VKImageUtils.h"
#include "../Memory/VKDeviceMemory.h"
#include "../Memory/VKDeviceMemoryManager.h"
#include "../Command/VKCommandContext.h"
#include "../../TextureUtils.h"
#include "../../../Core/CoreUtils.h"
#include <LLGL/Backend/Vulkan/NativeHandle.h>
#include "../VKTypes.h"
#include "../VKCore.h"
#include "../../../Core/Assertion.h"
#include "../../../Core/Exception.h"
#include "../../../Core/PrintfUtils.h"
#include <algorithm>

#if VK_ANDROID_external_memory_android_hardware_buffer
#   include "../Platform/Android/VKAndroidHardwareBuffer.h"
#endif


namespace LLGL
{


// Maps the specified format to a swizzle format, or identity swizzle if texture swizzling is not necessary
static VKSwizzleFormat MapToVKSwizzleFormat(const Format format)
{
    if (format == Format::A8UNorm)
        return VKSwizzleFormat::Alpha;
    else
        return VKSwizzleFormat::RGBA;
}

VKTexture::VKTexture(
    VkDevice                    device,
    VKDeviceMemoryManager&      deviceMemoryMngr,
    const TextureDescriptor&    desc,
    VKYcbcrConversionPool*      ycbcrConversionPool)
:
    Texture          { desc.type, desc.bindFlags         },
    device_          { device                            },
    externalMemory_  { device, vkFreeMemory              },
    isExternal_      { (desc.external != nullptr)        },
    image_           { device                            },
    imageView_       { device, vkDestroyImageView        },
    format_          { VKTypes::Map(desc.format)         },
    swizzleFormat_   { MapToVKSwizzleFormat(desc.format) },
    deviceMemoryMngr_{ deviceMemoryMngr                  }
{
    if (desc.external != nullptr)
    {
        /* Import external image with its own dedicated memory; this also acquires the Y'CbCr conversion */
        CreateExternalImage(device, desc, ycbcrConversionPool);
    }
    else
    {
        /* Acquire Y'CbCr conversion that is shared with the samplers this texture will be sampled with */
        if (desc.ycbcrConversion != nullptr && IsYcbcrConversionRequired(*desc.ycbcrConversion))
        {
            LLGL_ASSERT_PTR(ycbcrConversionPool);
            ycbcrConversion_ = ycbcrConversionPool->Acquire(*desc.ycbcrConversion);
        }

        /* Create Vulkan image and allocate memory region */
        CreateImage(device, desc);
        image_.AllocateMemoryRegion(deviceMemoryMngr);
    }

    if (desc.debugName != nullptr)
        SetDebugName(desc.debugName);
}

VKTexture::~VKTexture()
{
    /* Image and dedicated memory hold their own reference to the external image, so the texture's reference can be released first */
    ReleaseExternalHandle();
}

bool VKTexture::GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize)
{
    if (auto* nativeHandleVK = GetTypedNativeHandle<Vulkan::ResourceNativeHandle>(nativeHandle, nativeHandleSize))
    {
        nativeHandleVK->type                    = Vulkan::ResourceNativeType::Image;
        nativeHandleVK->image.image             = GetVkImage();
        nativeHandleVK->image.imageView         = GetVkImageView();
        nativeHandleVK->image.imageLayout       = GetVkImageLayout();
        nativeHandleVK->image.format            = GetVkFormat();
        nativeHandleVK->image.extent            = GetVkExtent();
        nativeHandleVK->image.numMipLevels      = GetNumMipLevels();
        nativeHandleVK->image.numArrayLayers    = GetNumArrayLayers();
        nativeHandleVK->image.sampleCountBits   = GetSampleCountBits();
        nativeHandleVK->image.imageUsageFlags   = GetUsageFlags();
        return true;
    }
    return false;
}

void VKTexture::SetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize)
{
    auto* nativeHandleVK = GetTypedNativeHandle<Vulkan::ResourceNativeHandle>(nativeHandle, nativeHandleSize);

    //image_.ReleaseMemoryRegion(deviceMemoryMngr_);

    image_.SetVkImage(nativeHandleVK->image.image);
    image_.SetVkImageLayout(nativeHandleVK->image.imageLayout);
    SetVkFormat(nativeHandleVK->image.format);
    SetVkExtent(nativeHandleVK->image.extent);
    SetNumMipLevels(nativeHandleVK->image.numMipLevels);
    SetNumArrayLayers(nativeHandleVK->image.numArrayLayers);
    SetSampleCountBits(nativeHandleVK->image.sampleCountBits);
    SetUsageFlags(nativeHandleVK->image.imageUsageFlags);

    //image_.AllocateMemoryRegion(deviceMemoryMngr_);

    CreateInternalImageView(deviceMemoryMngr_.GetVkDevice());
}

Extent3D VKTexture::GetMipExtent(std::uint32_t mipLevel) const
{
    switch (GetType())
    {
        case TextureType::Texture1D:
        case TextureType::Texture1DArray:
            return
            {
                std::max(1u, extent_.width  >> mipLevel),
                numArrayLayers_,
                1u
            };
        case TextureType::Texture2D:
        case TextureType::Texture2DArray:
        case TextureType::TextureCube:
        case TextureType::TextureCubeArray:
        case TextureType::Texture2DMS:
        case TextureType::Texture2DMSArray:
            return
            {
                std::max(1u, extent_.width  >> mipLevel),
                std::max(1u, extent_.height >> mipLevel),
                numArrayLayers_
            };
        case TextureType::Texture3D:
            return
            {
                std::max(1u, extent_.width  >> mipLevel),
                std::max(1u, extent_.height >> mipLevel),
                std::max(1u, extent_.depth  >> mipLevel)
            };
    }
    return { 0u, 0u, 0u };
}

TextureDescriptor VKTexture::GetDesc() const
{
    TextureDescriptor texDesc;

    texDesc.type        = GetType();
    texDesc.bindFlags   = GetBindFlags();
    texDesc.miscFlags   = 0;
    texDesc.format      = GetFormat();
    texDesc.arrayLayers = GetNumArrayLayers();
    texDesc.mipLevels   = GetNumMipLevels();

    switch (texDesc.type)
    {
        case TextureType::Texture1D:
        case TextureType::Texture1DArray:
            texDesc.extent.width    = extent_.width;
            texDesc.extent.height   = 1u;
            texDesc.extent.depth    = 1u;
            break;

        case TextureType::Texture2D:
        case TextureType::Texture2DArray:
            texDesc.extent.width    = extent_.width;
            texDesc.extent.height   = extent_.height;
            texDesc.extent.depth    = 1u;
            break;

        case TextureType::Texture3D:
            texDesc.extent.width    = extent_.width;
            texDesc.extent.height   = extent_.height;
            texDesc.extent.depth    = extent_.depth;
            break;

        case TextureType::TextureCube:
        case TextureType::TextureCubeArray:
            texDesc.extent.width    = extent_.width;
            texDesc.extent.height   = extent_.height;
            texDesc.extent.depth    = 1u;
            break;

        case TextureType::Texture2DMS:
        case TextureType::Texture2DMSArray:
            texDesc.extent.width    = extent_.width;
            texDesc.extent.height   = extent_.height;
            texDesc.extent.depth    = 1u;
            texDesc.samples         = static_cast<std::uint32_t>(sampleCountBits_);
            texDesc.miscFlags       |= MiscFlags::FixedSamples;
            break;
    }

    return texDesc;
}

// Maps the format from Alpha swizzling to RGBA
static Format MapVKSwizzleFormatAlpha(const Format format)
{
    switch (format)
    {
        case Format::R8UNorm:   return Format::A8UNorm;
        default:                return format;
    }
}

// Returns the texture format for the specified texture swizzling
static Format MapVKSwizzleFormat(const Format format, const VKSwizzleFormat swizzle)
{
    switch (swizzle)
    {
        case VKSwizzleFormat::Alpha:    return MapVKSwizzleFormatAlpha(format);
        default:                        return format;
    }
}

Format VKTexture::GetFormat() const
{
    /* Translate internal format depending on texture swizzle since A8UNorm is not natively supported in VkFormat */
    const Format format = VKTypes::Unmap(GetVkFormat());
    return MapVKSwizzleFormat(format, swizzleFormat_);
}

SubresourceFootprint VKTexture::GetSubresourceFootprint(std::uint32_t mipLevel) const
{
    const Extent3D extent{ extent_.width, extent_.height, extent_.depth };
    SubresourceFootprint footprint = CalcPackedSubresourceFootprint(GetType(), GetFormat(), extent, mipLevel, GetNumArrayLayers());
    footprint.size = GetAlignedSize(footprint.size, static_cast<std::uint64_t>(image_.GetMemoryRequirements().alignment));
    return footprint;
}

void VKTexture::SetDebugName(const char* name)
{
    #if VK_EXT_debug_marker
    VKSetDebugName(device_, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(GetVkImage()), name);
    #endif
}

// Maps the TextureSwizzleRGBA::a component to a different value for the "Alpha" swizzle format
static VkComponentSwizzle GetVkComponentAlphaComponent(const TextureSwizzle swizzleAlpha)
{
    switch (swizzleAlpha)
    {
        case TextureSwizzle::Alpha: return VK_COMPONENT_SWIZZLE_R;      // Only alpha component can be mapped to another component
        case TextureSwizzle::Zero:  return VK_COMPONENT_SWIZZLE_ZERO;   // Zero is allowed as fixed value
        case TextureSwizzle::One:   return VK_COMPONENT_SWIZZLE_ONE;    // One is allowed as fixed value
        default:                    return VK_COMPONENT_SWIZZLE_ZERO;   // Use zero as default value
    }
}

static void ConvertVkComponentMapping(VkComponentMapping& dst, const TextureSwizzleRGBA& src, VKSwizzleFormat swizzleFormat)
{
    switch (swizzleFormat)
    {
        case VKSwizzleFormat::RGBA: // Identity mapping
        {
            dst.r = VKTypes::ToVkComponentSwizzle(src.r);
            dst.g = VKTypes::ToVkComponentSwizzle(src.g);
            dst.b = VKTypes::ToVkComponentSwizzle(src.b);
            dst.a = VKTypes::ToVkComponentSwizzle(src.a);
        }
        break;

        case VKSwizzleFormat::Alpha:
        {
            dst.r = VK_COMPONENT_SWIZZLE_ZERO;
            dst.g = VK_COMPONENT_SWIZZLE_ZERO;
            dst.b = VK_COMPONENT_SWIZZLE_ZERO;
            dst.a = GetVkComponentAlphaComponent(src.a);
        }
        break;
    }
}

// Returns the identity component mapping. Swizzling for Y'CbCr images is part of the conversion, not of the image view.
static VkComponentMapping GetIdentityVkComponentMapping()
{
    VkComponentMapping components;
    {
        components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    }
    return components;
}

void VKTexture::CreateImageView(
    VkDevice                    device,
    const TextureSubresource&   subresource,
    Format                      format,
    VKPtr<VkImageView>&         outImageView)
{
    /* Images with Y'CbCr conversion must be viewed with their own format and the same conversion */
    VkSamplerYcbcrConversionInfo conversionInfo;
    const void* pNext = GetYcbcrConversionInfo(conversionInfo);

    const VkFormat viewVkFormat = (pNext != nullptr ? format_ : VKTypes::Map(format));
    VkImageSubresourceRange subresourceRange;
    {
        subresourceRange.aspectMask     = VKImageUtils::GetExclusiveVkImageAspect(viewVkFormat); //TODO: allow stencil-component to be selected
        subresourceRange.baseMipLevel   = subresource.baseMipLevel;
        subresourceRange.levelCount     = subresource.numMipLevels;
        subresourceRange.baseArrayLayer = subresource.baseArrayLayer;
        subresourceRange.layerCount     = subresource.numArrayLayers;
    }
    VkComponentMapping components = {};
    if (pNext != nullptr)
        components = GetIdentityVkComponentMapping();
    else
        ConvertVkComponentMapping(components, TextureSwizzleRGBA{}, swizzleFormat_);
    image_.CreateVkImageView(
        device,
        VKTypes::Map(GetType()),
        viewVkFormat,
        subresourceRange,
        outImageView,
        &components,
        pNext
    );
}

void VKTexture::CreateImageView(
    VkDevice                        device,
    const TextureViewDescriptor&    textureViewDesc,
    VKPtr<VkImageView>&             outImageView)
{
    /* Images with Y'CbCr conversion must be viewed with their own format and the same conversion */
    VkSamplerYcbcrConversionInfo conversionInfo;
    const void* pNext = GetYcbcrConversionInfo(conversionInfo);

    const VkFormat viewVkFormat = (pNext != nullptr ? format_ : VKTypes::Map(textureViewDesc.format));
    VkImageSubresourceRange subresourceRange;
    {
        subresourceRange.aspectMask     = VKImageUtils::GetExclusiveVkImageAspect(viewVkFormat); //TODO: allow stencil-component to be selected
        subresourceRange.baseMipLevel   = textureViewDesc.subresource.baseMipLevel;
        subresourceRange.levelCount     = textureViewDesc.subresource.numMipLevels;
        subresourceRange.baseArrayLayer = textureViewDesc.subresource.baseArrayLayer;
        subresourceRange.layerCount     = textureViewDesc.subresource.numArrayLayers;
    }
    VkComponentMapping components = {};
    if (pNext != nullptr)
        components = GetIdentityVkComponentMapping();
    else
        ConvertVkComponentMapping(components, textureViewDesc.swizzle, swizzleFormat_);
    image_.CreateVkImageView(
        device,
        (pNext != nullptr ? VKTypes::Map(GetType()) : VKTypes::Map(textureViewDesc.type)),
        viewVkFormat,
        subresourceRange,
        outImageView,
        &components,
        pNext
    );
}

static bool UsageFlagsAllowImageViews(VkImageUsageFlags flags)
{
    /* Vulkan only alows image views on images that were created with these usage flags */
    constexpr VkImageUsageFlags requiredFlags =
    (
        VK_IMAGE_USAGE_SAMPLED_BIT                              |
        VK_IMAGE_USAGE_STORAGE_BIT                              |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT                     |
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT             |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT                 |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT                     |
      //VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR |
      //VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT             |
      //VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR                 |
      //VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR                 |
      //VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR                 |
      //VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR                 |
      //VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM                   |
      //VK_IMAGE_USAGE_SAMPLE_BLOCK_MATCH_BIT_QCOM              |
        0
    );
    return ((flags & requiredFlags) != 0);
}

void VKTexture::CreateInternalImageView(VkDevice device)
{
    if (UsageFlagsAllowImageViews(GetUsageFlags()))
    {
        VkImageSubresourceRange subresourceRange;
        {
            subresourceRange.aspectMask     = VKImageUtils::GetExclusiveVkImageAspect(format_); //TODO: allow stencil-component to be selected
            subresourceRange.baseMipLevel   = 0;
            subresourceRange.levelCount     = GetNumMipLevels();
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount     = GetNumArrayLayers();
        }
        VkSamplerYcbcrConversionInfo conversionInfo;
        const void* pNext = GetYcbcrConversionInfo(conversionInfo);

        VkComponentMapping components = {};
        if (pNext != nullptr)
            components = GetIdentityVkComponentMapping();
        else
            ConvertVkComponentMapping(components, TextureSwizzleRGBA{}, swizzleFormat_);

        image_.CreateVkImageView(device, VKTypes::Map(GetType()), format_, subresourceRange, imageView_, &components, pNext);
    }
}

bool VKTexture::IsMultiPlanar() const
{
    return VKImageUtils::IsMultiPlanarVkFormat(format_);
}

VkImageLayout VKTexture::TransitionImageLayout(
    VKCommandContext&           context,
    VkImageLayout               newLayout,
    bool                        flushBarrier)
{
    const TextureSubresource fullSubresource{ 0, numArrayLayers_, 0, numMipLevels_ };
    VkImageLayout oldLayout = image_.TransitionImageLayout(context, GetVkFormat(), newLayout, fullSubresource);
    if (flushBarrier)
        context.FlushBarriers();
    return oldLayout;
}

VkImageLayout VKTexture::TransitionImageLayout(
    VKCommandContext&           context,
    VkImageLayout               newLayout,
    const TextureSubresource&   subresource,
    bool                        flushBarrier)
{
    VkImageLayout oldLayout = image_.TransitionImageLayout(context, GetVkFormat(), newLayout, subresource);
    if (flushBarrier)
        context.FlushBarriers();
    return oldLayout;
}


/*
 * ======= Private: =======
 */

// see https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#resources-image-views-compatibility
static VkImageCreateFlags GetVkImageCreateFlags(const TextureDescriptor& desc)
{
    VkImageCreateFlags createFlags = 0;

    /* Allow all SRVs to be interpreted with a different image format */
    if ((desc.bindFlags & BindFlags::Sampled) != 0)
        createFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    /*
    We only use VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT at the moment, to support cube maps.
    VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT is only required to make 3D textures compatible with 2D-array views, which LLGL does not support.
    */
    switch (desc.type)
    {
        case TextureType::TextureCube:
        case TextureType::TextureCubeArray:
            createFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            break;
        default:
            break;
    }

    return createFlags;
}

static VkImageType GetVkImageType(const TextureType textureType)
{
    if (textureType == TextureType::Texture3D)
        return VK_IMAGE_TYPE_3D;
    if (textureType == TextureType::Texture1D || textureType == TextureType::Texture1DArray)
        return VK_IMAGE_TYPE_1D;
    return VK_IMAGE_TYPE_2D;
}

static VkExtent3D GetVkImageExtent3D(const TextureDescriptor& desc, const VkImageType imageType)
{
    VkExtent3D extent;

    switch (imageType)
    {
        case VK_IMAGE_TYPE_1D:
            extent.width    = std::max(1u, desc.extent.width);
            extent.height   = 1u;
            extent.depth    = 1u;
            break;

        case VK_IMAGE_TYPE_2D:
            if (IsCubeTexture(desc.type))
            {
                /* Width and height must be equal for cube textures in Vulkan */
                extent.width    = std::max(1u, std::max(desc.extent.width, desc.extent.height));
                extent.height   = extent.width;
            }
            else
            {
                extent.width    = std::max(1u, desc.extent.width);
                extent.height   = std::max(1u, desc.extent.height);
            }
            extent.depth    = 1u;
            break;

        case VK_IMAGE_TYPE_3D:
            extent.width    = std::max(1u, desc.extent.width);
            extent.height   = std::max(1u, desc.extent.height);
            extent.depth    = std::max(1u, desc.extent.depth);
            break;

        default:
            extent.width    = 1u;
            extent.height   = 1u;
            extent.depth    = 1u;
            break;
    }

    return extent;
}

static std::uint32_t GetVkImageArrayLayers(const TextureDescriptor& desc, const VkImageType imageType)
{
    switch (imageType)
    {
        case VK_IMAGE_TYPE_1D:
        case VK_IMAGE_TYPE_2D:
            return std::max(1u, desc.arrayLayers);

        default:
            return 1u;
    }
}

//TODO:
//returned value must be a bit value from "VkImageFormatProperties::sampleCounts"
//that was returned by "vkGetPhysicalDeviceImageFormatProperties"
static VkSampleCountFlagBits GetVkImageSampleCountFlags(const TextureDescriptor& desc)
{
    if (IsMultiSampleTexture(desc.type))
        return VKTypes::ToVkSampleCountBits(desc.samples);
    else
        return VK_SAMPLE_COUNT_1_BIT;
}

static VkImageUsageFlags GetVkImageUsageFlags(const TextureDescriptor& desc)
{
    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    /* Enable TRANSFER_SRC_BIT image usage when MIP-maps are enabled, CPU read access or copy source binding is requested */
//  if (IsMipMappedTexture(desc) || (desc.cpuAccessFlags & CPUAccessFlags::Read) || (desc.bindFlags & BindFlags::CopySrc) != 0)
//      usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    /* Enable either color or depth-stencil ATTACHMENT_BIT image usage when attachment usage is enabled */
    if ((desc.bindFlags & BindFlags::ColorAttachment) != 0)
        usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    else if ((desc.bindFlags & BindFlags::DepthStencilAttachment) != 0)
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    /* Enable sampling the image */
    if ((desc.bindFlags & BindFlags::Sampled) != 0)
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

    /* Enable load/store operations on the image */
    if ((desc.bindFlags & BindFlags::Storage) != 0)
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;

    #if 0//???
    /* Enable input attachment bit when used for reading AND as attachment */
    if ( (desc.bindFlags & (BindFlags::Sampled         | BindFlags::Storage               )) != 0 &&
         (desc.bindFlags & (BindFlags::ColorAttachment | BindFlags::DepthStencilAttachment)) != 0 )
    {
        usageFlags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    #endif

    return usageFlags;
}

void VKTexture::CreateImage(VkDevice device, const TextureDescriptor& desc)
{
    /* Setup texture parameters */
    VkImageType imageType = GetVkImageType(desc.type);

    extent_             = GetVkImageExtent3D(desc, imageType);
    numMipLevels_       = NumMipLevels(desc);
    numArrayLayers_     = GetVkImageArrayLayers(desc, imageType);
    sampleCountBits_    = GetVkImageSampleCountFlags(desc);
    usageFlags_         = GetVkImageUsageFlags(desc);

    VkImageCreateFlags createFlags = GetVkImageCreateFlags(desc);

    if (IsMultiPlanar())
    {
        /*
        Multi-planar images are only sampled with a Y'CbCr conversion and uploaded plane by plane.
        They cannot have MIP-maps, array layers, or be used as attachments or storage images.
        */
        numMipLevels_       = 1;
        numArrayLayers_     = 1;
        sampleCountBits_    = VK_SAMPLE_COUNT_1_BIT;
        usageFlags_         = (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        createFlags         = 0;
    }

    /* Create image object */
    image_.CreateVkImage(
        device,
        imageType,
        format_,
        extent_,
        numMipLevels_,
        numArrayLayers_,
        createFlags,
        sampleCountBits_,
        usageFlags_
    );
}

void VKTexture::CreateExternalImage(VkDevice device, const TextureDescriptor& desc, VKYcbcrConversionPool* ycbcrConversionPool)
{
    const ExternalImageDescriptor& externalDesc = *desc.external;

    #if VK_ANDROID_external_memory_android_hardware_buffer

    if (externalDesc.type != ExternalImageType::AndroidHardwareBuffer || externalDesc.handle == nullptr)
        LLGL_TRAP("cannot create Vulkan texture from external image with null handle or unsupported type");

    auto* buffer = static_cast<AHardwareBuffer*>(externalDesc.handle);

    /* Query properties of hardware buffer and register its format features for Y'CbCr conversions of this external format */
    VKAndroidHardwareBufferProperties props;
    if (!VKQueryAndroidHardwareBufferProperties(device, buffer, props))
        LLGL_TRAP("failed to query properties of Android hardware buffer for Vulkan texture");

    if (ycbcrConversionPool != nullptr)
        ycbcrConversionPool->RegisterExternalFormatFeatures(props.externalFormat, props.formatFeatures);

    if (desc.ycbcrConversion != nullptr && IsYcbcrConversionRequired(*desc.ycbcrConversion))
    {
        LLGL_ASSERT_PTR(ycbcrConversionPool);
        ycbcrConversion_ = ycbcrConversionPool->Acquire(*desc.ycbcrConversion);
    }

    /* Images with an opaque format must use the external format, which requires a Y'CbCr conversion */
    const bool useExternalFormat = (props.format == VK_FORMAT_UNDEFINED || (ycbcrConversion_ && ycbcrConversion_->GetDesc().externalFormat != 0));
    if (useExternalFormat)
    {
        if (!ycbcrConversion_)
            LLGL_TRAP("Android hardware buffer with opaque format requires a Y'CbCr conversion (see TextureDescriptor::ycbcrConversion)");
        if (ycbcrConversion_->GetDesc().externalFormat != props.externalFormat)
        {
            LLGL_TRAP(
                "mismatch between external format of Y'CbCr conversion (0x%016" PRIX64 ") and Android hardware buffer (0x%016" PRIX64 ")",
                ycbcrConversion_->GetDesc().externalFormat, props.externalFormat
            );
        }
    }

    format_             = (useExternalFormat ? VK_FORMAT_UNDEFINED : props.format);
    extent_             = props.extent;
    numMipLevels_       = 1;
    numArrayLayers_     = 1;
    sampleCountBits_    = VK_SAMPLE_COUNT_1_BIT;
    usageFlags_         = VK_IMAGE_USAGE_SAMPLED_BIT; // Images with an external format must only be sampled

    /* Create image that can be bound to the memory of the hardware buffer */
    VkExternalFormatANDROID externalFormatInfo;
    {
        externalFormatInfo.sType            = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
        externalFormatInfo.pNext            = nullptr;
        externalFormatInfo.externalFormat   = (useExternalFormat ? props.externalFormat : 0);
    }
    VkExternalMemoryImageCreateInfo externalImageInfo;
    {
        externalImageInfo.sType             = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalImageInfo.pNext             = &externalFormatInfo;
        externalImageInfo.handleTypes       = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    }
    image_.CreateVkImage(
        device,
        VK_IMAGE_TYPE_2D,
        format_,
        extent_,
        numMipLevels_,
        numArrayLayers_,
        0,
        sampleCountBits_,
        usageFlags_,
        &externalImageInfo
    );

    /* Import memory of hardware buffer as dedicated allocation */
    VKImportAndroidHardwareBufferMemory(device, buffer, props, image_.GetVkImage(), externalMemory_);

    /* Keep a reference to the hardware buffer as long as this texture is alive */
    VKAcquireAndroidHardwareBuffer(buffer);
    externalHandle_ = buffer;

    /*
    The content of the external image is owned by its producer (e.g. a video decoder) and is transferred from the foreign queue family
    with oldLayout=GENERAL in each CommandBuffer::AcquireExternalTexture, so the layout is tracked as GENERAL from the beginning.
    VK_IMAGE_LAYOUT_UNDEFINED is not used here, because it permits the implementation to discard the content the producer wrote.
    Note that the native image starts out in VK_IMAGE_LAYOUT_UNDEFINED, so if a validation layer reports a layout mismatch
    for the first acquisition of an image, the first transition must use VK_IMAGE_LAYOUT_UNDEFINED instead.
    */
    image_.OverrideVkImageLayout(VK_IMAGE_LAYOUT_GENERAL);

    #else // VK_ANDROID_external_memory_android_hardware_buffer

    (void)device;
    (void)externalDesc;
    (void)ycbcrConversionPool;
    LLGL_TRAP("external images are not supported by the Vulkan backend on this platform");

    #endif // /VK_ANDROID_external_memory_android_hardware_buffer
}

void VKTexture::ReleaseExternalHandle()
{
    #if VK_ANDROID_external_memory_android_hardware_buffer
    if (externalHandle_ != nullptr)
    {
        VKReleaseAndroidHardwareBuffer(static_cast<AHardwareBuffer*>(externalHandle_));
        externalHandle_ = nullptr;
    }
    #endif
}

const void* VKTexture::GetYcbcrConversionInfo(VkSamplerYcbcrConversionInfo& outInfo) const
{
    if (ycbcrConversion_)
    {
        outInfo.sType       = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
        outInfo.pNext       = nullptr;
        outInfo.conversion  = ycbcrConversion_->GetVkSamplerYcbcrConversion();
        return &outInfo;
    }
    return nullptr;
}


} // /namespace LLGL



// ================================================================================
