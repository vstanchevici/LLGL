/*
 * VKPoolSizeAccumulator.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKPoolSizeAccumulator.h"
#include "../../../Core/Assertion.h"
#include <LLGL/Utils/ForRange.h>


namespace LLGL
{


// Returns the zero-based index of a descriptor pool for the specified descriptor type
static std::uint32_t GetPoolIndex(VkDescriptorType type)
{
    LLGL_ASSERT(type >= VK_DESCRIPTOR_TYPE_SAMPLER && type <= VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
    return static_cast<std::uint32_t>(type);
}

// Maximum number of descriptors a combined image sampler can consume.
// Combined image samplers with a Y'CbCr conversion consume up to one descriptor per plane (see VkSamplerYcbcrConversionImageFormatProperties::combinedImageSamplerDescriptorCount).
// This upper bound is used for all combined image samplers instead of querying the exact count per format, which only over-allocates a few descriptors.
static constexpr std::uint32_t g_maxCombinedImageSamplerDescriptorCount = 3;

void VKPoolSizeAccumulator::Accumulate(VkDescriptorType type, std::uint32_t count)
{
    const std::uint32_t poolIndex = GetPoolIndex(type);
    if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        count *= g_maxCombinedImageSamplerDescriptorCount;
    countsPerType_[poolIndex] += count;
}

void VKPoolSizeAccumulator::Finalize()
{
    for_range(i, numDescriptorTypes)
    {
        if (countsPerType_[i] > 0)
        {
            VkDescriptorPoolSize poolSizeInfo;
            {
                poolSizeInfo.type               = static_cast<VkDescriptorType>(i);
                poolSizeInfo.descriptorCount    = countsPerType_[i];
            }
            poolSizes_.push_back(poolSizeInfo);
        }
    }
}


} // /namespace LLGL



// ================================================================================
