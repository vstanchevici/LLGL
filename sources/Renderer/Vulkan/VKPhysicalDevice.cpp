/*
 * VKDevicePhysical.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKPhysicalDevice.h"
#include "Ext/VKExtensionRegistry.h"
#include "VKCore.h"
#include "VKTypes.h"
#include "RenderState/VKGraphicsPSO.h"
#include "../../Core/Vendor.h"
#include "../../Core/Assertion.h"
#include <LLGL/Constants.h>
#include <string>
#include <cstring>
#include <set>
#include <limits>
#include <algorithm>


namespace LLGL
{


static const char* g_requiredVulkanExtensions[] =
{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_MAINTENANCE1_EXTENSION_NAME,
    nullptr,
};

static bool CheckDeviceExtensionSupport(
    VkPhysicalDevice                    physicalDevice,
    const char* const*                  requiredExtensions,
    std::size_t                         numRequiredExtensions,
    std::vector<VkExtensionProperties>& outSupportedExtensions)
{
    /* Check if device supports all required extensions */
    outSupportedExtensions = VKQueryDeviceExtensionProperties(physicalDevice);
    std::set<std::string> unsupported(requiredExtensions, requiredExtensions + numRequiredExtensions);

    for (const VkExtensionProperties& ext : outSupportedExtensions)
    {
        if (unsupported.empty())
            break;
        else
            unsupported.erase(ext.extensionName);
    }

    /* No required extensions must remain */
    return unsupported.empty();
}

static bool IsPhysicalDeviceSuitable(
    VkPhysicalDevice                    physicalDevice,
    std::vector<VkExtensionProperties>& outSupportedExtensions)
{
    /* Check if physical devices supports at least these extensions */
    std::vector<VkExtensionProperties> extensions;
    bool suitable = CheckDeviceExtensionSupport(
        physicalDevice,
        g_requiredVulkanExtensions,
        (sizeof(g_requiredVulkanExtensions) / sizeof(g_requiredVulkanExtensions[0]) - 1),
        extensions
    );

    if (suitable)
    {
        /* Store all supported extensions */
        outSupportedExtensions = std::move(extensions);
        return true;
    }

    return false;
}

static bool IsPreferredDeviceVendor(DeviceVendor vendor, long preferredDeviceFlags)
{
    switch (vendor)
    {
        case DeviceVendor::NVIDIA:  return ((preferredDeviceFlags & RenderSystemFlags::PreferNVIDIA) != 0);
        case DeviceVendor::AMD:     return ((preferredDeviceFlags & RenderSystemFlags::PreferAMD   ) != 0);
        case DeviceVendor::Intel:   return ((preferredDeviceFlags & RenderSystemFlags::PreferIntel ) != 0);
        default:                    return false;
    }
}

bool VKPhysicalDevice::PickPhysicalDevice(VkInstance instance, const ArrayView<const char*>& supportedInstanceExtensions, long preferredDeviceFlags)
{
    /* Query all physical devices and pick suitable */
    std::vector<VkPhysicalDevice> physicalDevices = VKQueryPhysicalDevices(instance);

    auto TryPickPhysicalDevice = [this, &supportedInstanceExtensions](VkPhysicalDevice device) -> bool
    {
        if (!IsPhysicalDeviceSuitable(device, supportedExtensions_))
        {
            /* Device doesn't support required extensions */
            return false;
        }

        /* Store reference to all extension names */
        for (const VkExtensionProperties& extension : supportedExtensions_)
            supportedExtensionNames_.insert(extension.extensionName);

        if (!EnableExtensions(g_requiredVulkanExtensions, true))
        {
            /* Stop considering this physical device, because some required extensions are not supported */
            supportedExtensionNames_.clear();
            return false;
        }

        /* Store device and store properties */
        physicalDevice_ = device;
        EnableExtensions(GetOptionalExtensions());
        QueryDeviceInfo();

        return true;
    };

    if (preferredDeviceFlags != 0)
    {
        /* Try to find preferred device */
        for (VkPhysicalDevice device : physicalDevices)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            const DeviceVendor vendor = GetVendorByID(properties.vendorID);
            if (IsPreferredDeviceVendor(vendor, preferredDeviceFlags))
            {
                if (TryPickPhysicalDevice(device))
                    return true;
            }
        }
    }

    /* Pick first available device */
    for (VkPhysicalDevice device : physicalDevices)
    {
        if (TryPickPhysicalDevice(device))
            return true;
    }

    return false;
}

void VKPhysicalDevice::LoadPhysicalDeviceWeakRef(
    VkPhysicalDevice                physicalDevice,
    const ArrayView<const char*>&   enabledDeviceExtensions,
    const void*                     enabledDeviceFeatures)
{
    LLGL_ASSERT(physicalDevice != VK_NULL_HANDLE);
    LLGL_ASSERT(physicalDevice_ == VK_NULL_HANDLE, "physical Vulkan device already set");
    physicalDevice_ = physicalDevice;

    /*
    LLGL cannot enable any extension for a device it did not create, so only the extensions the client enabled are considered available.
    Otherwise, the rendering capabilities would report features whose extensions were never enabled for this device.
    */
    supportedExtensions_ = VKQueryDeviceExtensionProperties(physicalDevice);
    EnableCustomDeviceExtensions(enabledDeviceExtensions);

    QueryDeviceInfo();

    /* The features of the custom device are not known to LLGL, so only use what the client specified */
    QueryCustomDeviceFeatures(enabledDeviceFeatures);
}

static std::vector<Format> GetDefaultSupportedVKTextureFormats()
{
    return
    {
        Format::A8UNorm,
        Format::R8UNorm,            Format::R8SNorm,            Format::R8UInt,             Format::R8SInt,
        Format::R16UNorm,           Format::R16SNorm,           Format::R16UInt,            Format::R16SInt,            Format::R16Float,
        Format::R32UInt,            Format::R32SInt,            Format::R32Float,
        Format::R64Float,
        Format::RG8UNorm,           Format::RG8SNorm,           Format::RG8UInt,            Format::RG8SInt,
        Format::RG16UNorm,          Format::RG16SNorm,          Format::RG16UInt,           Format::RG16SInt,           Format::RG16Float,
        Format::RG32UInt,           Format::RG32SInt,           Format::RG32Float,
        Format::RG64Float,
        Format::RGB8UNorm,          Format::RGB8UNorm_sRGB,     Format::RGB8SNorm,          Format::RGB8UInt,           Format::RGB8SInt,
        Format::RGB16UNorm,         Format::RGB16SNorm,         Format::RGB16UInt,          Format::RGB16SInt,          Format::RGB16Float,
        Format::RGB32UInt,          Format::RGB32SInt,          Format::RGB32Float,
        Format::RGB64Float,
        Format::RGBA8UNorm,         Format::RGBA8UNorm_sRGB,    Format::RGBA8SNorm,         Format::RGBA8UInt,          Format::RGBA8SInt,
        Format::RGBA16UNorm,        Format::RGBA16SNorm,        Format::RGBA16UInt,         Format::RGBA16SInt,         Format::RGBA16Float,
        Format::RGBA32UInt,         Format::RGBA32SInt,         Format::RGBA32Float,
        Format::RGBA64Float,
        Format::BGRA8UNorm,         Format::BGRA8UNorm_sRGB,    Format::BGRA8SNorm,         Format::BGRA8UInt,          Format::BGRA8SInt,
        Format::RGB10A2UNorm,       Format::RGB10A2UInt,        Format::RG11B10Float,       Format::RGB9E5Float,        Format::BGR5A1UNorm,
        Format::D16UNorm,           Format::D24UNormS8UInt,     Format::D32Float,           Format::D32FloatS8X24UInt,
    };
}

static std::vector<Format> GetCompressedVKTextureFormatsBC()
{
    return
    {
        Format::BC1UNorm,   Format::BC1UNorm_sRGB,
        Format::BC2UNorm,   Format::BC2UNorm_sRGB,
        Format::BC3UNorm,   Format::BC3UNorm_sRGB,
        Format::BC4UNorm,   Format::BC4SNorm,
        Format::BC5UNorm,   Format::BC5SNorm,
        Format::BC6HUFloat, Format::BC6HSFloat,
        Format::BC7UNorm,   Format::BC7UNorm_sRGB,
    };
}

static std::vector<Format> GetCompressedVKTextureFormatsASTC()
{
    return
    {
        Format::ASTC4x4,    Format::ASTC4x4_sRGB,
        Format::ASTC5x4,    Format::ASTC5x4_sRGB,
        Format::ASTC5x5,    Format::ASTC5x5_sRGB,
        Format::ASTC6x5,    Format::ASTC6x5_sRGB,
        Format::ASTC6x6,    Format::ASTC6x6_sRGB,
        Format::ASTC8x5,    Format::ASTC8x5_sRGB,
        Format::ASTC8x6,    Format::ASTC8x6_sRGB,
        Format::ASTC8x8,    Format::ASTC8x8_sRGB,
        Format::ASTC10x5,   Format::ASTC10x5_sRGB,
        Format::ASTC10x6,   Format::ASTC10x6_sRGB,
        Format::ASTC10x8,   Format::ASTC10x8_sRGB,
        Format::ASTC10x10,  Format::ASTC10x10_sRGB,
        Format::ASTC12x10,  Format::ASTC12x10_sRGB,
        Format::ASTC12x12,  Format::ASTC12x12_sRGB,
    };
}

static std::vector<Format> GetCompressedVKTextureFormatsETC2()
{
    return
    {
        Format::ETC2UNorm, Format::ETC2UNorm_sRGB,
    };
}

// Structure for the Vulkan pipeline cache ID
struct VKPipelineCacheID
{
    std::uint32_t version;                          // VkPipelineCacheHeaderVersion (VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
    std::uint32_t vendorID;                         // VkPhysicalDeviceProperties::vendorID
    std::uint32_t deviceID;                         // VkPhysicalDeviceProperties::deviceID
    std::uint8_t  pipelineCacheUUID[VK_UUID_SIZE];  // VkPhysicalDeviceProperties::pipelineCacheUUID
};

static void GetVKPipelineCacheID(const VkPhysicalDeviceProperties& properties, std::vector<char>& outCacheID)
{
    outCacheID.resize(sizeof(VKPipelineCacheID));
    VKPipelineCacheID* dst = reinterpret_cast<VKPipelineCacheID*>(outCacheID.data());
    {
        dst->version  = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
        dst->vendorID = properties.vendorID;
        dst->deviceID = properties.deviceID;
        ::memcpy(dst->pipelineCacheUUID, properties.pipelineCacheUUID, sizeof(properties.pipelineCacheUUID));
    }
}

void VKPhysicalDevice::QueryRendererInfo(RendererInfo& info)
{
    /* Map properties to output renderer info */
    info.rendererName           = ("Vulkan " + VKApiVersionToString(properties_.apiVersion));
    info.deviceName             = properties_.deviceName;
    info.vendorName             = GetVendorName(GetVendorByID(properties_.vendorID));
    info.shadingLanguageName    = "SPIR-V";
    GetVKPipelineCacheID(properties_, info.pipelineCacheID);
}

void VKPhysicalDevice::QueryRenderingCaps(RenderingCapabilities& caps)
{
    /* Map limits to output rendering capabilites */
    const VkPhysicalDeviceFeatures& features = features_.features;
    const VkPhysicalDeviceLimits& limits = properties_.limits;

    /* Query common attributes */
    caps.screenOrigin                               = ScreenOrigin::UpperLeft;
    caps.clippingRange                              = ClippingRange::ZeroToOne;
    caps.shadingLanguages                           = { ShadingLanguage::SPIRV, ShadingLanguage::SPIRV_100 };
    caps.textureFormats                             = GetDefaultSupportedVKTextureFormats();

    if (features.textureCompressionBC != VK_FALSE)
    {
        const std::vector<Format> compressedFormatsBC = GetCompressedVKTextureFormatsBC();
        caps.textureFormats.insert(caps.textureFormats.end(), compressedFormatsBC.begin(), compressedFormatsBC.end());
    }

    if (features.textureCompressionASTC_LDR != VK_FALSE)
    {
        const std::vector<Format> compressedFormatsASTC = GetCompressedVKTextureFormatsASTC();
        caps.textureFormats.insert(caps.textureFormats.end(), compressedFormatsASTC.begin(), compressedFormatsASTC.end());
    }

    if (features.textureCompressionETC2 != VK_FALSE)
    {
        const std::vector<Format> compressedFormatsETC2 = GetCompressedVKTextureFormatsETC2();
        caps.textureFormats.insert(caps.textureFormats.end(), compressedFormatsETC2.begin(), compressedFormatsETC2.end());
    }

    const bool hasSamplerYcbcrConversion = HasExtension(VKExt::KHR_sampler_ycbcr_conversion);
    if (hasSamplerYcbcrConversion)
    {
        /* Multi-planar formats must be sampleable with at least one chroma location to be usable with a Y'CbCr conversion */
        for (Format format : { Format::NV12, Format::P010, Format::YUV420P })
        {
            VkFormatProperties formatProperties;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, VKTypes::Map(format), &formatProperties);

            constexpr VkFormatFeatureFlags requiredFeatures     = (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
            constexpr VkFormatFeatureFlags chromaLocationFlags  = (VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT | VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT);

            const VkFormatFeatureFlags optimalFeatures = formatProperties.optimalTilingFeatures;
            if ((optimalFeatures & requiredFeatures) == requiredFeatures && (optimalFeatures & chromaLocationFlags) != 0)
                caps.textureFormats.push_back(format);
        }
    }

    /* Query features */
    caps.features.hasRenderTargets                  = true;
    caps.features.has3DTextures                     = true;
    caps.features.hasCubeTextures                   = true;
    caps.features.hasArrayTextures                  = true;
    caps.features.hasCubeArrayTextures              = (features.imageCubeArray != VK_FALSE);
    caps.features.hasMultiSampleTextures            = true;
    caps.features.hasMultiSampleArrayTextures       = true;
    caps.features.hasTextureViews                   = true;
    caps.features.hasTextureViewSwizzle             = true;
    caps.features.hasTextureViewFormatSwizzle       = true;
    caps.features.hasBufferViews                    = true;
    caps.features.hasConstantBuffers                = true;
    caps.features.hasStorageBuffers                 = true;
    caps.features.hasGeometryShaders                = (features.geometryShader != VK_FALSE);
    caps.features.hasTessellationShaders            = (features.tessellationShader != VK_FALSE);
    caps.features.hasTessellatorStage               = caps.features.hasTessellationShaders;
    caps.features.hasComputeShaders                 = true;
    caps.features.hasInstancing                     = true;
    caps.features.hasOffsetInstancing               = true;
    caps.features.hasIndirectDrawing                = (features.drawIndirectFirstInstance != VK_FALSE);
    caps.features.hasViewportArrays                 = (features.multiViewport != VK_FALSE);
    caps.features.hasConservativeRasterization      = SupportsExtension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
    caps.features.hasStreamOutputs                  = SupportsExtension(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME);
    caps.features.hasLogicOp                        = (features.logicOp != VK_FALSE);
    caps.features.hasPipelineStatistics             = (features.pipelineStatisticsQuery != VK_FALSE);
    caps.features.hasRenderCondition                = SupportsExtension(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);
    caps.features.hasPipelineCaching                = true;
    caps.features.hasSamplerYcbcrConversion         = hasSamplerYcbcrConversion;
    caps.features.hasExternalImageAndroid           = (hasSamplerYcbcrConversion && HasExtension(VKExt::ANDROID_external_memory_android_hardware_buffer));

    /* Query limits */
    caps.limits.lineWidthRange[0]                   = limits.lineWidthRange[0];
    caps.limits.lineWidthRange[1]                   = limits.lineWidthRange[1];
    caps.limits.maxTextureArrayLayers               = limits.maxImageArrayLayers;
    caps.limits.maxColorAttachments                 = limits.maxColorAttachments;
    caps.limits.maxPatchVertices                    = limits.maxTessellationPatchSize;
    caps.limits.max1DTextureSize                    = limits.maxImageDimension1D;
    caps.limits.max2DTextureSize                    = limits.maxImageDimension2D;
    caps.limits.max3DTextureSize                    = limits.maxImageDimension3D;
    caps.limits.maxCubeTextureSize                  = limits.maxImageDimensionCube;
    caps.limits.maxAnisotropy                       = static_cast<std::uint32_t>(limits.maxSamplerAnisotropy);
    caps.limits.maxComputeShaderWorkGroups[0]       = limits.maxComputeWorkGroupCount[0];
    caps.limits.maxComputeShaderWorkGroups[1]       = limits.maxComputeWorkGroupCount[1];
    caps.limits.maxComputeShaderWorkGroups[2]       = limits.maxComputeWorkGroupCount[2];
    caps.limits.maxComputeShaderWorkGroupSize[0]    = limits.maxComputeWorkGroupSize[0];
    caps.limits.maxComputeShaderWorkGroupSize[1]    = limits.maxComputeWorkGroupSize[1];
    caps.limits.maxComputeShaderWorkGroupSize[2]    = limits.maxComputeWorkGroupSize[2];
    caps.limits.maxViewports                        = std::min(limits.maxViewports, LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS);
    caps.limits.maxViewportSize[0]                  = limits.maxViewportDimensions[0];
    caps.limits.maxViewportSize[1]                  = limits.maxViewportDimensions[1];
    caps.limits.maxBufferSize                       = std::numeric_limits<VkDeviceSize>::max();
    caps.limits.maxConstantBufferSize               = limits.maxUniformBufferRange;
    caps.limits.maxStreamOutputs                    = transformFeedbackProps_.maxTransformFeedbackBuffers;
    caps.limits.maxTessFactor                       = limits.maxTessellationGenerationLevel;
    caps.limits.minConstantBufferAlignment          = limits.minUniformBufferOffsetAlignment;
    caps.limits.minSampledBufferAlignment           = limits.minStorageBufferOffsetAlignment; // Use SSBO for both sampled and storage buffers
    caps.limits.minStorageBufferAlignment           = limits.minStorageBufferOffsetAlignment;
    caps.limits.maxColorBufferSamples               = VKTypes::GetMaxVkSampleCounts(limits.framebufferColorSampleCounts);
    caps.limits.maxDepthBufferSamples               = VKTypes::GetMaxVkSampleCounts(limits.framebufferDepthSampleCounts);
    caps.limits.maxStencilBufferSamples             = VKTypes::GetMaxVkSampleCounts(limits.framebufferStencilSampleCounts);
    caps.limits.maxNoAttachmentSamples              = VKTypes::GetMaxVkSampleCounts(limits.framebufferNoAttachmentsSampleCounts);
    caps.limits.storageResourceStageFlags           = StageFlags::AllStages;
}

void VKPhysicalDevice::QueryPipelineLimits(VKGraphicsPipelineLimits& pipelineLimits)
{
    /* Map limits to output rendering capabilites */
    const VkPhysicalDeviceLimits& limits = properties_.limits;

    /* Store graphics pipeline spcific limitations */
    pipelineLimits.lineWidthRange[0]    = limits.lineWidthRange[0];
    pipelineLimits.lineWidthRange[1]    = limits.lineWidthRange[1];
    pipelineLimits.lineWidthGranularity = limits.lineWidthGranularity;

    /*
    TODO: extension limits
    - VkPhysicalDeviceTransformFeedbackFeaturesEXT
    - VkPhysicalDeviceTransformFeedbackPropertiesEXT
    */
}

VKDevice VKPhysicalDevice::CreateLogicalDevice(VkDevice customLogicalDevice)
{
    VKDevice device;
    if (customLogicalDevice != VK_NULL_HANDLE)
        device.LoadLogicalDeviceWeakRef(physicalDevice_, customLogicalDevice);
    else
        device.CreateLogicalDevice(physicalDevice_, &features_, GetExtensionNames());
    return device;
}

std::uint32_t VKPhysicalDevice::FindMemoryType(std::uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) const
{
    return VKFindMemoryType(memoryProperties_, memoryTypeBits, properties);
}

bool VKPhysicalDevice::SupportsExtension(const char* extension) const
{
    auto it = std::find_if(
        supportedExtensionNames_.begin(),
        supportedExtensionNames_.end(),
        [extension](const char* entry)
        {
            return (std::strcmp(extension, entry) == 0);
        }
    );
    return (it != supportedExtensionNames_.end());
}


/*
 * ======= Private: =======
 */

void VKPhysicalDevice::EnableCustomDeviceExtensions(const ArrayView<const char*>& extensions)
{
    for (const VkExtensionProperties& supportedExtension : supportedExtensions_)
    {
        for (const char* name : extensions)
        {
            if (name != nullptr && std::strcmp(name, supportedExtension.extensionName) == 0)
            {
                /*
                Store pointers to the names of the device's extension properties, so we don't depend on the lifetime of the client's strings.
                Only client enabled extensions are registered as supported, see LoadPhysicalDeviceWeakRef.
                */
                supportedExtensionNames_.insert(supportedExtension.extensionName);
                enabledExtensionNames_.push_back(supportedExtension.extensionName);
                break;
            }
        }
    }
}

// Base structure to iterate a Vulkan pNext chain.
struct VKBaseInStructure
{
    VkStructureType             sType;
    const VKBaseInStructure*    pNext;
};

void VKPhysicalDevice::QueryCustomDeviceFeatures(const void* enabledDeviceFeatures)
{
    isSamplerYcbcrConversionEnabled_ = false;

    /* Search the client's feature chain for the sampler Y'CbCr conversion feature */
    for (auto* next = static_cast<const VKBaseInStructure*>(enabledDeviceFeatures); next != nullptr; next = next->pNext)
    {
        switch (next->sType)
        {
            #if VK_KHR_sampler_ycbcr_conversion
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR:
            {
                auto* features = reinterpret_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR*>(next);
                if (features->samplerYcbcrConversion != VK_FALSE)
                    isSamplerYcbcrConversionEnabled_ = true;
            }
            break;
            #endif

            #if VK_VERSION_1_2
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
            {
                auto* features = reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(next);
                if (features->samplerYcbcrConversion != VK_FALSE)
                    isSamplerYcbcrConversionEnabled_ = true;
            }
            break;
            #endif

            default:
            break;
        }
    }
}

bool VKPhysicalDevice::EnableExtensions(const char** extensions, bool required)
{
    for (; *extensions != nullptr; ++extensions)
    {
        const char* name = *extensions;
        if (supportedExtensionNames_.find(name) != supportedExtensionNames_.end())
        {
            /* Add name to enabled Vulkan extensions */
            enabledExtensionNames_.push_back(name);
        }
        else if (required)
        {
            /* Cancel search and return with error */
            enabledExtensionNames_.clear();
            return false;
        }
    }
    return true;
}

void VKPhysicalDevice::QueryDeviceInfo()
{
    /* Query physical device features and properties with extensions */
    QueryDeviceFeatures();
    QueryDeviceProperties();
    QueryDeviceMemoryProperties();
}

struct VKBaseStructureInfo
{
    VkStructureType sType;
    void*           pNext;
};

void VKPhysicalDevice::QueryDeviceFeatures()
{
    #if VK_KHR_get_physical_device_properties2

    /*
    Features that were promoted to Vulkan 1.1 can be used without their extension if the effective API version is 1.1 or later,
    which is the minimum of the version of the instance this device is used with and the version of the device itself.
    */
    VkPhysicalDeviceProperties basicProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &basicProperties);
    const bool isVulkan11Supported = (std::min(instanceApiVersion_, basicProperties.apiVersion) >= VK_API_VERSION_1_1);

    VKBaseStructureInfo* currentDesc = nullptr;

    auto AppendFeaturesDesc = [&currentDesc](void* descPtr, VkStructureType type) -> void
    {
        /* Chain next descriptor into previous one */
        currentDesc->pNext = descPtr;

        /* Write structure type and store next descriptor */
        auto baseDescPtr = static_cast<VKBaseStructureInfo*>(descPtr);
        {
            baseDescPtr->sType = type;
        }
        currentDesc = baseDescPtr;
    };

    features_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    currentDesc = reinterpret_cast<VKBaseStructureInfo*>(&features_);

    #if VK_EXT_nested_command_buffer
    if (SupportsExtension(VK_EXT_NESTED_COMMAND_BUFFER_EXTENSION_NAME))
        AppendFeaturesDesc(&nestedCmdBufferFeatures_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT);
    #endif

    #if VK_EXT_transform_feedback
    if (SupportsExtension(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME))
        AppendFeaturesDesc(&transformFeedbackFeatures_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT);
    #endif

    #if VK_KHR_imageless_framebuffer
    if (SupportsExtension(VK_KHR_IMAGELESS_FRAMEBUFFER_EXTENSION_NAME))
        AppendFeaturesDesc(&imagelessFramebufferFeatures_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR);
    #endif

    #if VK_KHR_sampler_ycbcr_conversion
    const bool hasSamplerYcbcrConversionFeatures = (isVulkan11Supported || SupportsExtension(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME));
    if (hasSamplerYcbcrConversionFeatures)
        AppendFeaturesDesc(&samplerYcbcrConversionFeatures_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR);
    #endif

    vkGetPhysicalDeviceFeatures2(physicalDevice_, &features_);

    /* Features chained into 'features_' are enabled when LLGL creates the logical device (see VKDevice::CreateLogicalDevice) */
    #if VK_KHR_sampler_ycbcr_conversion
    isSamplerYcbcrConversionEnabled_ = (hasSamplerYcbcrConversionFeatures && samplerYcbcrConversionFeatures_.samplerYcbcrConversion != VK_FALSE);
    #endif

    #else // VK_KHR_get_physical_device_properties2

    vkGetPhysicalDeviceFeatures(physicalDevice_, &(features_.features));

    #endif // /VK_KHR_get_physical_device_properties2
}

void VKPhysicalDevice::QueryDeviceProperties()
{
    #if VK_KHR_get_physical_device_properties2

    VKBaseStructureInfo* currentDesc = nullptr;

    auto ChainDescriptor = [&currentDesc](void* descPtr, VkStructureType type)
    {
        /* Chain next descriptor into previous one */
        currentDesc->pNext = descPtr;

        /* Write structure type and store next descriptor */
        auto baseDescPtr = reinterpret_cast<VKBaseStructureInfo*>(descPtr);
        {
            baseDescPtr->sType = type;
        }
        currentDesc = baseDescPtr;
    };

    /* Chain extensions into output descriptor */
    VkPhysicalDeviceProperties2 propertiesExt = {};
    propertiesExt.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    currentDesc = reinterpret_cast<VKBaseStructureInfo*>(&propertiesExt);

    if (SupportsExtension(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME))
        ChainDescriptor(&conservRasterProps_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT);

    #if VK_EXT_transform_feedback
    if (SupportsExtension(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME))
        ChainDescriptor(&transformFeedbackProps_, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT);
    #endif

    /* Query device properties with extension "VK_KHR_get_physical_device_properties2" */
    vkGetPhysicalDeviceProperties2(physicalDevice_, &propertiesExt);

    /* Store primary device properties */
    properties_ = propertiesExt.properties;

    #else // VK_KHR_get_physical_device_properties2

    vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);

    #endif // /VK_KHR_get_physical_device_properties2
}

void VKPhysicalDevice::QueryDeviceMemoryProperties()
{
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
}


} // /namespace LLGL



// ================================================================================
