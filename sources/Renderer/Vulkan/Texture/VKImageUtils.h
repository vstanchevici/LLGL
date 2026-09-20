/*
 * VKImageUtils.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_IMAGE_UTILS_H
#define LLGL_VK_IMAGE_UTILS_H


#include <cstdint>
#include "../Vulkan.h"
#include <LLGL/TextureFlags.h>


namespace LLGL
{

namespace VKImageUtils
{


// Initializes VkImageResolve or VkImageCopy.
template <typename TDst>
void InitVkImageRegion(
    TDst&                   outRegion,
    const TextureRegion&    textureRegion,
    const Offset3D&         srcOffset,
    VkImageAspectFlags      srcAspectFlags,
    VkImageAspectFlags      dstAspectFlags)
{
    outRegion.srcSubresource.aspectMask     = srcAspectFlags;
    outRegion.srcSubresource.mipLevel       = textureRegion.subresource.baseMipLevel;
    outRegion.srcSubresource.baseArrayLayer = textureRegion.subresource.baseArrayLayer;
    outRegion.srcSubresource.layerCount     = textureRegion.subresource.numArrayLayers;
    outRegion.srcOffset.x                   = srcOffset.x;
    outRegion.srcOffset.y                   = srcOffset.y;
    outRegion.srcOffset.z                   = srcOffset.z;
    outRegion.dstSubresource.aspectMask     = dstAspectFlags;
    outRegion.dstSubresource.mipLevel       = textureRegion.subresource.baseMipLevel;
    outRegion.dstSubresource.baseArrayLayer = textureRegion.subresource.baseArrayLayer;
    outRegion.dstSubresource.layerCount     = textureRegion.subresource.numArrayLayers;
    outRegion.dstOffset.x                   = textureRegion.offset.x;
    outRegion.dstOffset.y                   = textureRegion.offset.y;
    outRegion.dstOffset.z                   = textureRegion.offset.z;
    outRegion.extent.width                  = textureRegion.extent.width;
    outRegion.extent.height                 = textureRegion.extent.height;
    outRegion.extent.depth                  = textureRegion.extent.depth;
}

// Returns the image aspect for the specified Vulkan format
VkImageAspectFlags GetInclusiveVkImageAspect(VkFormat format);

// Returns the image aspect for the specified Vulkan format
VkImageAspectFlags GetExclusiveVkImageAspect(VkFormat format, bool preferStencilComponent = false);

// Describes a single plane of a multi-planar Vulkan format.
struct VKFormatPlane
{
    VkImageAspectFlagBits   aspect;         // VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_ASPECT_PLANE_1_BIT, or VK_IMAGE_ASPECT_PLANE_2_BIT.
    std::uint32_t           subsampleX;     // Horizontal divisor of the plane extent relative to the image extent.
    std::uint32_t           subsampleY;     // Vertical divisor of the plane extent relative to the image extent.
    std::uint32_t           bytesPerTexel;  // Size (in bytes) of a texel in this plane.
};

// Maximum number of planes in a multi-planar Vulkan format.
constexpr std::uint32_t maxNumVkFormatPlanes = 3;

// Returns true if the specified Vulkan format is a multi-planar format, e.g. VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
bool IsMultiPlanarVkFormat(VkFormat format);

// Returns the number of planes of the specified format that are supported for uploads and writes their layout to 'outPlanes', or 0 if the format is not supported.
std::uint32_t GetVkFormatPlanes(VkFormat format, VKFormatPlane (&outPlanes)[maxNumVkFormatPlanes]);


} // /namespace VKImageUtils

} // /namespace LLGL


#endif



// ================================================================================
