/*
 * VKImageUtils.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKImageUtils.h"
#include <LLGL/Constants.h>


namespace LLGL
{

namespace VKImageUtils
{


VkImageAspectFlags GetInclusiveVkImageAspect(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

VkImageAspectFlags GetExclusiveVkImageAspect(VkFormat format, bool preferStencilComponent)
{
    switch (format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return (preferStencilComponent ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT);
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

bool IsMultiPlanarVkFormat(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
            return true;
        default:
            return false;
    }
}

std::uint32_t GetVkFormatPlanes(VkFormat format, VKFormatPlane (&outPlanes)[maxNumVkFormatPlanes])
{
    switch (format)
    {
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            outPlanes[0] = { VK_IMAGE_ASPECT_PLANE_0_BIT, 1, 1, 1 }; // G8
            outPlanes[1] = { VK_IMAGE_ASPECT_PLANE_1_BIT, 2, 2, 2 }; // B8R8
            return 2;

        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
            outPlanes[0] = { VK_IMAGE_ASPECT_PLANE_0_BIT, 1, 1, 2 }; // G10X6
            outPlanes[1] = { VK_IMAGE_ASPECT_PLANE_1_BIT, 2, 2, 4 }; // B10X6R10X6
            return 2;

        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            outPlanes[0] = { VK_IMAGE_ASPECT_PLANE_0_BIT, 1, 1, 1 }; // G8
            outPlanes[1] = { VK_IMAGE_ASPECT_PLANE_1_BIT, 2, 2, 1 }; // B8
            outPlanes[2] = { VK_IMAGE_ASPECT_PLANE_2_BIT, 2, 2, 1 }; // R8
            return 3;

        default:
            return 0;
    }
}


} // /namespace VKImageUtils

} // /namespace LLGL



// ================================================================================
