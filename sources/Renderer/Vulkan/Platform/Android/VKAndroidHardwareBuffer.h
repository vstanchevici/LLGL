/*
 * VKAndroidHardwareBuffer.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_ANDROID_HARDWARE_BUFFER_H
#define LLGL_VK_ANDROID_HARDWARE_BUFFER_H


#include "../../Vulkan.h"
#include "../../VKPtr.h"
#include <cstdint>


struct AHardwareBuffer;

namespace LLGL
{


struct ExternalImageProperties;

// Properties of an Android hardware buffer for the Vulkan device.
struct VKAndroidHardwareBufferProperties
{
    VkDeviceSize                    allocationSize          = 0;
    std::uint32_t                   memoryTypeBits          = 0;
    VkFormat                        format                  = VK_FORMAT_UNDEFINED;
    std::uint64_t                   externalFormat          = 0;
    VkFormatFeatureFlags            formatFeatures          = 0;
    VkComponentMapping              components              = {};
    VkSamplerYcbcrModelConversion   suggestedYcbcrModel     = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    VkSamplerYcbcrRange             suggestedYcbcrRange     = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    VkChromaLocation                suggestedXChromaOffset  = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkChromaLocation                suggestedYChromaOffset  = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkExtent3D                      extent                  = { 0, 0, 1 };
};

// Queries the Vulkan properties of the specified Android hardware buffer. Returns false if the buffer cannot be imported.
bool VKQueryAndroidHardwareBufferProperties(VkDevice device, AHardwareBuffer* buffer, VKAndroidHardwareBufferProperties& outProperties);

// Converts the specified Vulkan properties into the LLGL external image properties.
void VKConvertAndroidHardwareBufferProperties(const VKAndroidHardwareBufferProperties& props, ExternalImageProperties& outProperties);

/*
Imports the memory of the specified Android hardware buffer as dedicated allocation for the specified image and binds it to that image.
The image must have been created with VkExternalMemoryImageCreateInfo and VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID.
*/
void VKImportAndroidHardwareBufferMemory(
    VkDevice                                    device,
    AHardwareBuffer*                            buffer,
    const VKAndroidHardwareBufferProperties&    props,
    VkImage                                     image,
    VKPtr<VkDeviceMemory>&                      outMemory
);

// Acquires a reference to the specified Android hardware buffer.
void VKAcquireAndroidHardwareBuffer(AHardwareBuffer* buffer);

// Releases a reference to the specified Android hardware buffer.
void VKReleaseAndroidHardwareBuffer(AHardwareBuffer* buffer);


} // /namespace LLGL


#endif



// ================================================================================
