/*
 * NativeHandle.h (Vulkan)
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VULKAN_NATIVE_HANDLE_H
#define LLGL_VULKAN_NATIVE_HANDLE_H


#include <cstdint>
#include <vulkan/vulkan.h>
#include <LLGL/Deprecated.h>


namespace LLGL
{

namespace Vulkan
{


/**
\brief Native handle structure for the Vulkan render system.
\see RenderSystem::GetNativeHandle
\see RenderSystemDescriptor::nativeHandle
*/
struct RenderSystemNativeHandle
{
    //! Native handle to the Vulkan instance.
    VkInstance          instance;

    //! Native handle to the physical Vulkan device.
    VkPhysicalDevice    physicalDevice;

    //! Native handle to the logical Vulkan device.
    VkDevice            device;

    //! Native handle to the command queue.
    VkQueue             queue;

    //! Family index to the native command queue.
    std::uint32_t       queueFamily;
};

/**
\brief Native handle structure for the Vulkan command buffer.
\see CommandBuffer::GetNativeHandle
*/
struct CommandBufferNativeHandle
{
    VkCommandBuffer commandBuffer;
};

/**
\brief Native handle structure for the Vulkan command buffer.
\see RenderPass::GetNativeHandle
*/
struct RenderPassNativeHandle
{
    VkRenderPass renderPass;
};

/**
\brief Native Vulkan resource type enumeration.
\see ResourceNativeHandle::type
*/
enum class ResourceNativeType
{
    /**
    \brief Native Vulkan VkBuffer type.
    \see ResourceNativeHandle::buffer
    */
    Buffer,

    /**
    \brief Native Vulkan VkImage type.
    \see ResourceNativeHandle::texture
    */
    Image,

    /**
    \brief Native Vulkan VkSampler type.
    \see ResourceNativeHandle::sampler
    */
    Sampler,
};

/**
\brief Native handle structure for a Vulkan resource.
\see Resource::GetNativeHandle
*/
struct ResourceNativeHandle
{
    struct NativeBuffer
    {
        VkBuffer buffer;    //!< Native Vulkan VkBuffer object.
    };

    struct NativeImage
    {
        VkImage                 image;              //!< Primary Vulkan image stored as native VkImage type.
        VkImageView             imageView;          //!< Current image view.
        VkImageLayout           imageLayout;        //!< Current image layout. This depends on resource transitioning.
        VkFormat                format;             //!< Native Vulkan image format.
        VkExtent3D              extent;             //!< Native Vulkan image extent. Does \e not include array layers.
        std::uint32_t           numMipLevels;       //!< Number of MIP-map levels.
        std::uint32_t           numArrayLayers;     //!< Number of array layers.
        VkSampleCountFlagBits   sampleCountBits;    //!< Sample count bitmask for multi-sampled textures.
        VkImageUsageFlags       imageUsageFlags;    //!< Image usag flags the texture was created with.

        /**
        \brief Device memory the image is bound to. May be VK_NULL_HANDLE.
        \remarks This is only used by Resource::SetNativeHandle with \c own set to true, in which case the memory is freed together with the image.
        GetNativeHandle always returns VK_NULL_HANDLE, since LLGL manages the memory of its own images in larger chunks.
        */
        VkDeviceMemory              memory;

        /**
        \brief Optional sampler Y'CbCr conversion the image must be viewed and sampled with. May be VK_NULL_HANDLE.
        \remarks If this is not null, \c ycbcrSampler must be a sampler that was created with the same conversion.
        The image view is created with this conversion and the texture can be bound to a combined texture-sampler
        whose sampler binding has BindFlags::SamplerYcbcrConversion. For images with an external format, \c format is VK_FORMAT_UNDEFINED.
        \remarks Textures that share the same conversion and sampler handles share the same pipeline variant.
        */
        VkSamplerYcbcrConversion    ycbcrConversion;

        /**
        \brief Sampler that was created with \c ycbcrConversion. It is used as immutable sampler of the combined texture-sampler. May be VK_NULL_HANDLE.
        \see BindFlags::SamplerYcbcrConversion
        */
        VkSampler                   ycbcrSampler;
    };

    struct NativeSampler
    {
        VkSampler                   sampler;            //!< Native Vulkan VkSampler object.
        VkSamplerYcbcrConversion    ycbcrConversion;    //!< Optional sampler Y'CbCr conversion the sampler was created with. May be VK_NULL_HANDLE.
    };

    /**
    \brief Specifies the native resource type.
    \remarks This allows to distinguish a resource between native Vulkan types.
    */
    ResourceNativeType  type;

    union
    {
        //! Buffer specific attriubtes.
        NativeBuffer    buffer;

        //! Texture specific attriubtes.
        NativeImage     image;

        //! Sampler specific attriubtes.
        NativeSampler   sampler;
    };
};


} // /namespace Vulkan

} // /namespace LLGL


#endif



// ================================================================================
