/*
 * VKCommandBuffer.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKCommandBuffer.h"
#include "VKCommandQueue.h"
#include "../VKPhysicalDevice.h"
#include "../VKSwapChain.h"
#include "../VKTypes.h"
#include "../Ext/VKExtensionRegistry.h"
#include "../Ext/VKExtensions.h"
#include "../RenderState/VKRenderPass.h"
#include "../RenderState/VKGraphicsPSO.h"
#include "../RenderState/VKComputePSO.h"
#include "../RenderState/VKResourceHeap.h"
#include "../RenderState/VKPredicateQueryHeap.h"
#include "../Texture/VKSampler.h"
#include "../Texture/VKTexture.h"
#include "../Texture/VKImageUtils.h"
#include "../Texture/VKRenderTarget.h"
#include "../Buffer/VKBuffer.h"
#include "../Buffer/VKBufferArray.h"
#include "../../CheckedCast.h"
#include "../../../Core/Assertion.h"
#include "../../../Core/CoreUtils.h"
#include <LLGL/Utils/ForRange.h>
#include <LLGL/Constants.h>
#include <LLGL/TypeInfo.h>
#include <LLGL/Log.h>
#include <cstddef>
#include <algorithm>

#include <LLGL/Backend/Vulkan/NativeHandle.h>


namespace LLGL
{


constexpr std::uint32_t VKCommandBuffer::maxNumCommandBuffers;

// Returns the maximum for a indirect multi draw command
static std::uint32_t GetMaxDrawIndirectCount(const VKPhysicalDevice& physicalDevice)
{
    if (physicalDevice.GetFeatures().features.multiDrawIndirect != VK_FALSE)
        return physicalDevice.GetProperties().limits.maxDrawIndirectCount;
    else
        return 1u;
}

VKCommandBuffer::VKCommandBuffer(
    const VKPhysicalDevice&         physicalDevice,
    VkDevice                        device,
    VkQueue                         commandQueue,
    VKDeviceMemoryManager&          deviceMemoryMngr,
    const VKQueueFamilyIndices&     queueFamilyIndices,
    const CommandBufferDescriptor&  desc)
:
    device_                 { device                                        },
    commandQueue_           { commandQueue                                  },
    commandPool_            { device, vkDestroyCommandPool                  },
    recordingFenceArray_    { VKPtr<VkFence>{ device, vkDestroyFence },
                              VKPtr<VkFence>{ device, vkDestroyFence },
                              VKPtr<VkFence>{ device, vkDestroyFence }      },
    numCommandBuffers_      { VKCommandBuffer::GetNumVkCommandBuffers(desc) },
    queuePresentFamily_     { queueFamilyIndices.presentFamily              },
    queueGraphicsFamily_    { queueFamilyIndices.graphicsFamily             },
    maxDrawIndirectCount_   { GetMaxDrawIndirectCount(physicalDevice)       },
    descriptorSetPoolArray_ { device,
                              device,
                              device                                        }
{
    /* Translate creation flags */
    if ((desc.flags & CommandBufferFlags::ImmediateSubmit) != 0)
    {
        immediateSubmit_    = true;
        usageFlags_         = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    else
    {
        if ((desc.flags & CommandBufferFlags::Secondary) != 0)
        {
            bufferLevel_ = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            if (desc.renderPass != nullptr)
            {
                auto* renderPassVK = LLGL_CAST(const VKRenderPass*, desc.renderPass);
                renderPass_ = renderPassVK->GetVkRenderPass();
                usageFlags_ |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            }
        }
        if ((desc.flags & CommandBufferFlags::MultiSubmit) == 0)
            usageFlags_ |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }

    /* Create native command buffer objects */
    CreateVkCommandPool(queueFamilyIndices.graphicsFamily);
    CreateVkCommandBuffers();
    CreateVkRecordingFences();
    CreateStagingBufferPools(deviceMemoryMngr, static_cast<VkDeviceSize>(desc.minStagingPoolSize));
}

VKCommandBuffer::~VKCommandBuffer()
{
    vkFreeCommandBuffers(device_, commandPool_, numCommandBuffers_, commandBufferArray_);
    for (VkCommandBuffer prologue : prologueBufferArray_)
    {
        if (prologue != VK_NULL_HANDLE)
            vkFreeCommandBuffers(device_, commandPool_, 1, &prologue);
    }
}

VkFence VKCommandBuffer::GetQueueSubmitFenceAndFlush()
{
    /*
    Flush recoring fence since we don't have to signal it more than once,
    until the same native command buffer is recorded again.
    */
    VkFence fence = recordingFence_;
    recordingFence_ = VK_NULL_HANDLE;
    recordingFenceDirty_[commandBufferIndex_] = true;
    return fence;
}

VkResult VKCommandBuffer::SubmitToQueue(VkQueue queue)
{
    /* Submit prologue that acquires external textures in the same batch, so its barriers are ordered before the commands of this buffer */
    VkCommandBuffer commandBuffers[2];
    std::uint32_t numCommandBuffers = 0;

    if (prologueRecorded_[commandBufferIndex_])
        commandBuffers[numCommandBuffers++] = prologueBufferArray_[commandBufferIndex_];
    commandBuffers[numCommandBuffers++] = commandBuffer_;

    return VKSubmitCommandBuffers(queue, numCommandBuffers, commandBuffers, GetQueueSubmitFenceAndFlush());
}

/* ----- Encoding ----- */

void VKCommandBuffer::Begin()
{
    /* Use next internal VkCommandBuffer object to reduce latency */
    AcquireNextBuffer();

    /* Initialize inheritance if this is a secondary command buffer */
    VkCommandBufferInheritanceInfo inheritanceInfo;
    if (IsSecondaryCmdBuffer())
    {
        inheritanceInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritanceInfo.pNext                   = nullptr;
        inheritanceInfo.renderPass              = renderPass_;
        inheritanceInfo.subpass                 = 0;
        inheritanceInfo.framebuffer             = VK_NULL_HANDLE;
        inheritanceInfo.occlusionQueryEnable    = VK_FALSE;
        inheritanceInfo.queryFlags              = 0;
        inheritanceInfo.pipelineStatistics      = 0;
    }

    /* Begin recording of current command buffer */
    VkCommandBufferBeginInfo beginInfo;
    {
        beginInfo.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext             = nullptr;
        beginInfo.flags             = usageFlags_;
        beginInfo.pInheritanceInfo  = (IsSecondaryCmdBuffer() ? &inheritanceInfo : nullptr);
    }
    VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
    VKThrowIfFailed(result, "failed to begin Vulkan command buffer");

    #if 0//TODO: optimize
    /* Reset all query pools that were in flight during last encoding */
    ResetQueryPoolsInFlight();
    #endif

    /* Reset record states to default values */
    recordState_                            = RecordState::OutsideRenderPass;
    framebufferRenderArea_.offset.x         = 0;
    framebufferRenderArea_.offset.y         = 0;
    framebufferRenderArea_.extent.width     = static_cast<std::uint32_t>(INT32_MAX); // Must avoid int32 overflow
    framebufferRenderArea_.extent.height    = static_cast<std::uint32_t>(INT32_MAX); // Must avoid int32 overflow
    hasDynamicScissorRect_                  = false;
}

void VKCommandBuffer::End()
{
    /*
    Transfer ownership of external textures back to their producer at the end of this command buffer and record the prologue to acquire them.
    Secondary command buffers pass their external textures to the primary command buffer (see Execute).
    */
    if (!externalTextures_.empty() && !IsSecondaryCmdBuffer())
    {
        RecordExternalTextureReleaseBarriers();
        RecordExternalTextureAcquirePrologue();
    }

    /* End encoding of current command buffer */
    VkResult result = vkEndCommandBuffer(commandBuffer_);
    VKThrowIfFailed(result, "failed to end Vulkan command buffer");

    /* Store new record state */
    recordState_ = RecordState::ReadyForSubmit;

    /* Execute command buffer right after encoding for immediate command buffers */
    if (IsImmediateCmdBuffer())
    {
        VkResult result = SubmitToQueue(commandQueue_);
        VKThrowIfFailed(result, "failed to submit command buffer to Vulkan graphics queue");
    }

    ResetBindingStates();
}

void VKCommandBuffer::Execute(CommandBuffer& secondaryCommandBuffer)
{
    auto& cmdBufferVK = LLGL_CAST(VKCommandBuffer&, secondaryCommandBuffer);
    VkCommandBuffer cmdBuffers[] = { cmdBufferVK.GetVkCommandBuffer() };
    vkCmdExecuteCommands(commandBuffer_, 1, cmdBuffers);

    /* Ownership of external textures is transferred by the primary command buffer */
    for (VKTexture* textureVK : cmdBufferVK.externalTextures_)
        TrackExternalTexture(*textureVK);
}

/* ----- Blitting ----- */

void VKCommandBuffer::UpdateBuffer(
    Buffer&         dstBuffer,
    std::uint64_t   dstOffset,
    const void*     data,
    std::uint64_t   dataSize)
{
    auto& dstBufferVK = LLGL_CAST(VKBuffer&, dstBuffer);

    const VkDeviceSize size     = static_cast<VkDeviceSize>(dataSize);
    const VkDeviceSize offset   = static_cast<VkDeviceSize>(dstOffset);

    constexpr VkDeviceSize k_limitForCmdUpdateBuffer = (1u << 16);

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        {
            if (size > k_limitForCmdUpdateBuffer)
                stagingBufferPools_[commandBufferIndex_].WriteStaged(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, data, size);
            else
                vkCmdUpdateBuffer(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, size, data);
            BufferPipelineBarrier(dstBufferVK.GetVkBuffer(), offset, size, VK_ACCESS_TRANSFER_WRITE_BIT, dstBufferVK.GetAccessFlags());
        }
        ResumeRenderPass();
    }
    else
    {
        if (size > k_limitForCmdUpdateBuffer)
            stagingBufferPools_[commandBufferIndex_].WriteStaged(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, data, size);
        else
            vkCmdUpdateBuffer(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, size, data);
        BufferPipelineBarrier(dstBufferVK.GetVkBuffer(), offset, size, VK_ACCESS_TRANSFER_WRITE_BIT, dstBufferVK.GetAccessFlags());
    }
}

void VKCommandBuffer::CopyBuffer(
    Buffer&         dstBuffer,
    std::uint64_t   dstOffset,
    Buffer&         srcBuffer,
    std::uint64_t   srcOffset,
    std::uint64_t   size)
{
    auto& dstBufferVK = LLGL_CAST(VKBuffer&, dstBuffer);
    auto& srcBufferVK = LLGL_CAST(VKBuffer&, srcBuffer);

    VkBufferCopy region;
    {
        region.srcOffset    = static_cast<VkDeviceSize>(srcOffset);
        region.dstOffset    = static_cast<VkDeviceSize>(dstOffset);
        region.size         = static_cast<VkDeviceSize>(size);
    }

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        vkCmdCopyBuffer(commandBuffer_, srcBufferVK.GetVkBuffer(), dstBufferVK.GetVkBuffer(), 1, &region);
        ResumeRenderPass();
    }
    else
        vkCmdCopyBuffer(commandBuffer_, srcBufferVK.GetVkBuffer(), dstBufferVK.GetVkBuffer(), 1, &region);
}

void VKCommandBuffer::CopyBufferFromTexture(
    Buffer&                 dstBuffer,
    std::uint64_t           dstOffset,
    Texture&                srcTexture,
    const TextureRegion&    srcRegion,
    std::uint32_t           rowStride,
    std::uint32_t           layerStride)
{
    auto& dstBufferVK = LLGL_CAST(VKBuffer&, dstBuffer);
    auto& srcTextureVK = LLGL_CAST(VKTexture&, srcTexture);

    VkBufferImageCopy region;
    {
        region.bufferOffset                     = dstOffset;
        region.bufferRowLength                  = rowStride;
        region.bufferImageHeight                = layerStride;
        region.imageSubresource.aspectMask      = VKImageUtils::GetInclusiveVkImageAspect(srcTextureVK.GetVkFormat());
        region.imageSubresource.mipLevel        = srcRegion.subresource.baseMipLevel;
        region.imageSubresource.baseArrayLayer  = srcRegion.subresource.baseArrayLayer;
        region.imageSubresource.layerCount      = srcRegion.subresource.numArrayLayers;
        region.imageOffset                      = VKTypes::ToVkOffset(srcRegion.offset);
        region.imageExtent                      = VKTypes::ToVkExtent(srcRegion.extent);
    }

    //TODO: context must detect if barriers are incompatible
    context_.BufferMemoryBarrier(dstBufferVK.GetVkBuffer(), 0, VK_WHOLE_SIZE, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageLayout oldLayout = srcTextureVK.TransitionImageLayout(context_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, true);

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        context_.CopyImageToBuffer(srcTextureVK, dstBufferVK, region);
        ResumeRenderPass();
    }
    else
        context_.CopyImageToBuffer(srcTextureVK, dstBufferVK, region);

    srcTextureVK.TransitionImageLayout(context_, oldLayout, true);
}

void VKCommandBuffer::FillBuffer(
    Buffer&         dstBuffer,
    std::uint64_t   dstOffset,
    std::uint32_t   value,
    std::uint64_t   fillSize)
{
    auto& dstBufferVK = LLGL_CAST(VKBuffer&, dstBuffer);

    /* Determine destination buffer range and ignore <dstOffset> if the whole buffer is meant to be filled */
    VkDeviceSize offset, size;
    if (fillSize == LLGL_WHOLE_SIZE)
    {
        offset  = 0;
        size    = VK_WHOLE_SIZE;
    }
    else
    {
        offset  = static_cast<VkDeviceSize>(dstOffset);
        size    = static_cast<VkDeviceSize>(fillSize);
    }

    /* Encode fill buffer command */
    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        vkCmdFillBuffer(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, size, value);
        ResumeRenderPass();
    }
    else
        vkCmdFillBuffer(commandBuffer_, dstBufferVK.GetVkBuffer(), offset, size, value);
}

void VKCommandBuffer::CopyTexture(
    Texture&                dstTexture,
    const TextureLocation&  dstLocation,
    Texture&                srcTexture,
    const TextureLocation&  srcLocation,
    const Extent3D&         extent)
{
    auto& dstTextureVK = LLGL_CAST(VKTexture&, dstTexture);
    auto& srcTextureVK = LLGL_CAST(VKTexture&, srcTexture);

    VkImageCopy region;
    {
        region.srcSubresource.aspectMask        = VKImageUtils::GetInclusiveVkImageAspect(srcTextureVK.GetVkFormat());
        region.srcSubresource.mipLevel          = srcLocation.mipLevel;
        region.srcSubresource.baseArrayLayer    = srcLocation.arrayLayer;
        region.srcSubresource.layerCount        = 1;
        region.srcOffset                        = VKTypes::ToVkOffset(srcLocation.offset);
        region.dstSubresource.aspectMask        = VKImageUtils::GetInclusiveVkImageAspect(dstTextureVK.GetVkFormat());
        region.dstSubresource.mipLevel          = dstLocation.mipLevel;
        region.dstSubresource.baseArrayLayer    = dstLocation.arrayLayer;
        region.dstSubresource.layerCount        = 1;
        region.dstOffset                        = VKTypes::ToVkOffset(dstLocation.offset);
        region.extent                           = VKTypes::ToVkExtent(extent);
    }

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        context_.CopyTexture(srcTextureVK, dstTextureVK, region);
        ResumeRenderPass();
    }
    else
        context_.CopyTexture(srcTextureVK, dstTextureVK, region);
}

void VKCommandBuffer::CopyTextureFromBuffer(
    Texture&                dstTexture,
    const TextureRegion&    dstRegion,
    Buffer&                 srcBuffer,
    std::uint64_t           srcOffset,
    std::uint32_t           rowStride,
    std::uint32_t           layerStride)
{
    auto& dstTextureVK = LLGL_CAST(VKTexture&, dstTexture);
    auto& srcBufferVK = LLGL_CAST(VKBuffer&, srcBuffer);

    VkBufferImageCopy region;
    {
        region.bufferOffset                     = srcOffset;
        region.bufferRowLength                  = rowStride;
        region.bufferImageHeight                = layerStride;
        region.imageSubresource.aspectMask      = VKImageUtils::GetInclusiveVkImageAspect(dstTextureVK.GetVkFormat());
        region.imageSubresource.mipLevel        = dstRegion.subresource.baseMipLevel;
        region.imageSubresource.baseArrayLayer  = dstRegion.subresource.baseArrayLayer;
        region.imageSubresource.layerCount      = dstRegion.subresource.numArrayLayers;
        region.imageOffset                      = VKTypes::ToVkOffset(dstRegion.offset);
        region.imageExtent                      = VKTypes::ToVkExtent(dstRegion.extent);
    }

    //TODO: context must detect if barriers are incompatible
    context_.BufferMemoryBarrier(srcBufferVK.GetVkBuffer(), 0, VK_WHOLE_SIZE, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_READ_BIT);
    VkImageLayout oldLayout = dstTextureVK.TransitionImageLayout(context_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        context_.CopyBufferToImage(srcBufferVK, dstTextureVK, region);
        ResumeRenderPass();
    }
    else
        context_.CopyBufferToImage(srcBufferVK, dstTextureVK, region);

    dstTextureVK.TransitionImageLayout(context_, oldLayout, true);
}

void VKCommandBuffer::CopyTextureFromFramebuffer(
    Texture&                dstTexture,
    const TextureRegion&    dstRegion,
    const Offset2D&         srcOffset)
{
    if (boundSwapChain_ == nullptr)
        return /*No bound framebuffer*/;

    if (dstRegion.extent.depth != 1 ||
        dstRegion.offset.x < 0      ||
        dstRegion.offset.y < 0      ||
        dstRegion.offset.z < 0)
    {
        return /*Out of bounds*/;
    }

    auto& dstTextureVK = LLGL_CAST(VKTexture&, dstTexture);

    if (IsInsideRenderPass())
    {
        PauseRenderPass();
        boundSwapChain_->CopyImage(
            context_,
            dstTextureVK.GetVkImage(),
            dstTextureVK.GetVkImageLayout(),
            dstRegion,
            currentColorBuffer_,
            srcOffset,
            dstTextureVK.GetVkFormat()
        );
        ResumeRenderPass();
    }
    else
    {
        boundSwapChain_->CopyImage(
            context_,
            dstTextureVK.GetVkImage(),
            dstTextureVK.GetVkImageLayout(),
            dstRegion,
            currentColorBuffer_,
            srcOffset,
            dstTextureVK.GetVkFormat()
        );
    }
}

void VKCommandBuffer::GenerateMips(Texture& texture)
{
    auto& textureVK = LLGL_CAST(VKTexture&, texture);

    /* Multi-planar and external images have no MIP-maps */
    if (textureVK.IsMultiPlanar() || textureVK.IsExternal())
        return;
    context_.GenerateMips(
        textureVK.GetVkImage(),
        textureVK.GetVkFormat(),
        textureVK.GetVkExtent(),
        TextureSubresource{ 0, textureVK.GetNumArrayLayers(), 0, textureVK.GetNumMipLevels() }
    );
}

void VKCommandBuffer::GenerateMips(Texture& texture, const TextureSubresource& subresource)
{
    auto& textureVK = LLGL_CAST(VKTexture&, texture);

    /* Multi-planar and external images have no MIP-maps */
    if (textureVK.IsMultiPlanar() || textureVK.IsExternal())
        return;

    const std::uint32_t maxNumMipLevels     = textureVK.GetNumMipLevels();
    const std::uint32_t maxNumArrayLayers   = textureVK.GetNumArrayLayers();

    if (subresource.baseMipLevel   < maxNumMipLevels   && subresource.numMipLevels   > 0 &&
        subresource.baseArrayLayer < maxNumArrayLayers && subresource.numArrayLayers > 0)
    {
        context_.GenerateMips(
            textureVK.GetVkImage(),
            textureVK.GetVkFormat(),
            textureVK.GetVkExtent(),
            subresource
        );
    }
}

/* ----- Viewport and Scissor ----- */

void VKCommandBuffer::SetViewport(const Viewport& viewport)
{
    /* Convert viewport to VkViewport type */
    VkViewport viewportVK;
    VKTypes::Convert(viewportVK, viewport);
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewportVK);
}

void VKCommandBuffer::SetViewports(std::uint32_t numViewports, const Viewport* viewports)
{
    VkViewport viewportsVK[LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS];

    /* Convert viewport to VkViewport types */
    numViewports = std::min(numViewports, LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS);
    for_range(i, numViewports)
        VKTypes::Convert(viewportsVK[i], viewports[i]);

    vkCmdSetViewport(commandBuffer_, 0, numViewports, viewportsVK);
}

void VKCommandBuffer::SetScissor(const Scissor& scissor)
{
    if (scissorEnabled_)
    {
        /* Convert scissor to VkRect2D type */
        VkRect2D scissorVK;
        VKTypes::Convert(scissorVK, scissor);
        vkCmdSetScissor(commandBuffer_, 0, 1, &scissorVK);
    }
}

void VKCommandBuffer::SetScissors(std::uint32_t numScissors, const Scissor* scissors)
{
    if (scissorEnabled_)
    {
        /* Convert scissor to VkRect2D types */
        VkRect2D scissorsVK[LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS];

        numScissors = std::min(numScissors, LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS);
        for_range(i, numScissors)
            VKTypes::Convert(scissorsVK[i], scissors[i]);

        vkCmdSetScissor(commandBuffer_, 0, numScissors, scissorsVK);
    }
}

/* ----- Input Assembly ------ */

//private
void VKCommandBuffer::BindVertexBuffer(VKBuffer& bufferVK)
{
    VkBuffer buffers[] = { bufferVK.GetVkBuffer() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, buffers, offsets);

    /* Store input-assembly state for slot 0 in case it's used for stream-output */
    if ((bufferVK.GetBindFlags() & BindFlags::StreamOutputBuffer) != 0)
    {
        iaState_.ia0VertexStride            = bufferVK.GetStride();
        iaState_.ia0XfbCounterBuffer        = bufferVK.GetVkBuffer();
        iaState_.ia0XfbCounterBufferOffset  = bufferVK.GetXfbCounterOffset();
    }
}

void VKCommandBuffer::SetVertexBuffer(Buffer& buffer)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    BindVertexBuffer(bufferVK);
}

void VKCommandBuffer::SetVertexBuffer(Buffer& buffer, std::uint32_t numVertexAttribs, const VertexAttribute* vertexAttribs)
{
    if (numVertexAttribs > 0 && vertexAttribs != nullptr)
    {
        auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
        bufferVK.SetStride(vertexAttribs[0].stride);
        BindVertexBuffer(bufferVK);
    }
}

void VKCommandBuffer::SetVertexBufferArray(BufferArray& bufferArray)
{
    auto& bufferArrayVK = LLGL_CAST(VKBufferArray&, bufferArray);
    vkCmdBindVertexBuffers(
        commandBuffer_,
        0,
        static_cast<std::uint32_t>(bufferArrayVK.GetBuffers().size()),
        bufferArrayVK.GetBuffers().data(),
        bufferArrayVK.GetOffsets().data()
    );
}

void VKCommandBuffer::SetIndexBuffer(Buffer& buffer)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    vkCmdBindIndexBuffer(commandBuffer_, bufferVK.GetVkBuffer(), 0, bufferVK.GetIndexType());
}

void VKCommandBuffer::SetIndexBuffer(Buffer& buffer, const Format format, std::uint64_t offset)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    vkCmdBindIndexBuffer(commandBuffer_, bufferVK.GetVkBuffer(), offset, VKTypes::ToVkIndexType(format));
}

/* ----- Resources ----- */

// Shader stages that can sample external textures
static constexpr VkPipelineStageFlags g_externalTextureStageMask =
(
    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT     |
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT   |
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
);

void VKCommandBuffer::SetResourceHeap(ResourceHeap& resourceHeap, std::uint32_t descriptorSet)
{
    if (boundPipelineState_ == nullptr)
        return /*No PSO bound*/;

    /* Bind resource heap to pipeline bind point and insert resource barrier into command buffer */
    auto& resourceHeapVK = LLGL_CAST(VKResourceHeap&, resourceHeap);
    if (!(descriptorSet < resourceHeapVK.GetVkDescriptorSets().size()))
        return /*Descriptor set out of bounds*/;

    VkDescriptorSet descriptorSetVK = resourceHeapVK.GetVkDescriptorSets()[descriptorSet];
    if (boundPipelineState_->HasYcbcrVariants())
    {
        /* Heap descriptor set must be bound again with the layout of each Y'CbCr variant */
        ycbcrHeapDescriptorSet_ = descriptorSetVK;
        if (boundPipelineLayout_ != VK_NULL_HANDLE)
            boundPipelineState_->BindHeapDescriptorSet(commandBuffer_, boundPipelineLayout_, descriptorSetVK);
    }
    else
        boundPipelineState_->BindHeapDescriptorSet(commandBuffer_, boundPipelineLayout_, descriptorSetVK);

    if (boundPipelineBarrier_ != nullptr)
        resourceHeapVK.SetBarrierSlots(*boundPipelineBarrier_, descriptorSet);
}

void VKCommandBuffer::SetResource(std::uint32_t descriptor, Resource& resource)
{
    if (boundBindingTable_ == nullptr)
        return /*No PSO bound*/;

    if (!(descriptor < boundBindingTable_->dynamicBindings.size()))
        return /*Out of bounds*/;

    /* Ownership of external textures is transferred implicitly when this command buffer is submitted */
    if (resource.GetResourceType() == ResourceType::Texture)
    {
        VKTexture& textureVK = LLGL_CAST(VKTexture&, resource);
        if (textureVK.IsExternal())
            TrackExternalTexture(textureVK);
    }

    /* Resources of PSOs with Y'CbCr variants are written once the variant is resolved; this may also switch the binding table */
    bool writeDescriptor = true;
    if (boundPipelineState_ != nullptr && boundPipelineState_->HasYcbcrVariants())
        writeDescriptor = SetYcbcrPipelineResource(descriptor, resource);

    const VKLayoutBinding& binding = boundBindingTable_->dynamicBindings[descriptor];
    if (binding.descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM)
        return /*Virtual binding*/;

    if (writeDescriptor)
        descriptorCache_->EmplaceDescriptor(resource, binding, descriptorSetWriter_);

    /* Update pipeline barrier slot */
    if (boundPipelineBarrier_ != nullptr)
    {
        if (resource.GetResourceType() == ResourceType::Buffer)
        {
            VKBuffer& bufferVK = LLGL_CAST(VKBuffer&, resource);
            boundPipelineBarrier_->SetBufferBarrier(binding.barrierSlot, bufferVK.GetVkBuffer());
        }
        else if (resource.GetResourceType() == ResourceType::Texture)
        {
            VKTexture& textureVK = LLGL_CAST(VKTexture&, resource);
            boundPipelineBarrier_->SetImageBarrier(binding.barrierSlot, textureVK.GetVkImage());
        }
    }
}

void VKCommandBuffer::ResourceBarrier(
    std::uint32_t       numBuffers,
    Buffer* const *     buffers,
    std::uint32_t       numTextures,
    Texture* const *    textures)
{
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    SmallVector<VkBufferMemoryBarrier, 32u>  bufferBarriers;
    SmallVector<VkImageMemoryBarrier, 32u>   imageBarriers;

    /* Preapre buffer barriers for read.write access */
    bufferBarriers.resize(numBuffers);

    for_range(i, numBuffers)
    {
        VkBufferMemoryBarrier& barrier = bufferBarriers[i];

        auto* bufferVK = LLGL_CAST(VKBuffer*, buffers[i]);

        barrier.sType                   = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.pNext                   = nullptr;
        barrier.srcAccessMask           = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask           = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex     = 0;
        barrier.dstQueueFamilyIndex     = 0;
        barrier.buffer                  = bufferVK->GetVkBuffer();
        barrier.offset                  = 0;
        barrier.size                    = VK_WHOLE_SIZE;
    }

    /* Prepare image barriers for texture read/write access */
    imageBarriers.resize(numTextures);

    for_range(i, numTextures)
    {
        VkImageMemoryBarrier& barrier = imageBarriers[i];

        auto* textureVK = LLGL_CAST(VKTexture*, textures[i]);

        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext                           = nullptr;
        barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout                       = textureVK->GetVkImageLayout();
        barrier.newLayout                       = textureVK->GetVkImageLayout();
        barrier.srcQueueFamilyIndex             = 0;
        barrier.dstQueueFamilyIndex             = 0;
        barrier.image                           = textureVK->GetVkImage();
        barrier.subresourceRange.aspectMask     = VKImageUtils::GetInclusiveVkImageAspect(textureVK->GetVkFormat());
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = textureVK->GetNumMipLevels();
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = textureVK->GetNumArrayLayers();
    }

    /* Encode pipeline barrier command into command buffer */
    vkCmdPipelineBarrier(
        commandBuffer_,
        srcStageMask,
        dstStageMask,
        0, // VkDependencyFlags
        0, // memoryBarrierCount
        nullptr, // pMemoryBarriers
        static_cast<std::uint32_t>(bufferBarriers.size()),
        bufferBarriers.data(),
        static_cast<std::uint32_t>(imageBarriers.size()),
        imageBarriers.data()
    );
}

/* ----- Render Passes ----- */

void VKCommandBuffer::BeginRenderPass(
    RenderTarget&       renderTarget,
    const RenderPass*   renderPass,
    std::uint32_t       numClearValues,
    const ClearValue*   clearValues,
    std::uint32_t       swapBufferIndex)
{
    LLGL_ASSERT(!IsSecondaryCmdBuffer());

    if (LLGL::IsInstanceOf<SwapChain>(renderTarget))
    {
        /* Get Vulkan swap-chain object */
        auto& swapChainVK = LLGL_CAST(VKSwapChain&, renderTarget);

        /* Store information about framebuffer attachments */
        boundSwapChain_                 = &swapChainVK;
        currentColorBuffer_             = swapChainVK.TranslateSwapIndex(swapBufferIndex);
        renderPass_                     = swapChainVK.GetSwapChainRenderPass().GetVkRenderPass();
        secondaryRenderPass_            = swapChainVK.GetSecondaryVkRenderPass();
        framebuffer_                    = swapChainVK.GetVkFramebuffer(currentColorBuffer_);
        framebufferRenderArea_.extent   = swapChainVK.GetVkExtent();
        numColorAttachments_            = swapChainVK.GetNumColorAttachments();
        hasDepthStencilAttachment_      = (swapChainVK.HasDepthAttachment() || swapChainVK.HasStencilAttachment());
    }
    else
    {
        /* Get Vulkan render target object and store its extent for subsequent commands */
        auto& renderTargetVK = LLGL_CAST(VKRenderTarget&, renderTarget);

        /* Store information about framebuffer attachments */
        renderPass_                     = renderTargetVK.GetVkRenderPass();
        secondaryRenderPass_            = renderTargetVK.GetSecondaryVkRenderPass();
        framebuffer_                    = renderTargetVK.GetVkFramebuffer();
        framebufferRenderArea_.extent   = renderTargetVK.GetVkExtent();
        numColorAttachments_            = renderTargetVK.GetNumColorAttachments();
        hasDepthStencilAttachment_      = (renderTargetVK.HasDepthAttachment() || renderTargetVK.HasStencilAttachment());

        renderTargetVK.OverrideImageLayoutsForRenderPass();
    }

    hasDynamicScissorRect_ = false;

    /* Uninitialized stack memory for clear values */
    VkClearValue clearValuesVK[LLGL_MAX_NUM_COLOR_ATTACHMENTS * 2 + 1];
    std::uint32_t numClearValuesVK = 0;

    /* Get native render pass object either from RenderTarget or RenderPass interface */
    if (renderPass != nullptr)
    {
        /* Get native VkRenderPass object */
        auto* renderPassVK = LLGL_CAST(const VKRenderPass*, renderPass);
        renderPass_ = renderPassVK->GetVkRenderPass();
        ConvertRenderPassClearValues(*renderPassVK, numClearValuesVK, clearValuesVK, numClearValues, clearValues);
    }

    /* Determine subpass contents */
    subpassContents_ =
    (
        #ifdef VK_EXT_nested_command_buffer
        HasExtension(VKExt::EXT_nested_command_buffer)
            ? VK_SUBPASS_CONTENTS_INLINE_AND_SECONDARY_COMMAND_BUFFERS_EXT
            : VK_SUBPASS_CONTENTS_INLINE
        #else
        VK_SUBPASS_CONTENTS_INLINE
        #endif
    );

    /* Record begin of render pass */
    VkRenderPassBeginInfo beginInfo;
    {
        beginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.pNext             = nullptr;
        beginInfo.renderPass        = renderPass_;
        beginInfo.framebuffer       = framebuffer_;
        beginInfo.renderArea        = framebufferRenderArea_;
        beginInfo.clearValueCount   = numClearValuesVK;
        beginInfo.pClearValues      = clearValuesVK;
    }
    vkCmdBeginRenderPass(commandBuffer_, &beginInfo, subpassContents_);

    /* Store new record state */
    recordState_ = RecordState::InsideRenderPass;
}

void VKCommandBuffer::EndRenderPass()
{
    LLGL_ASSERT(renderPass_ != VK_NULL_HANDLE);

    /* Record and of render pass */
    vkCmdEndRenderPass(commandBuffer_);

    /* Reset render pass and framebuffer attributes */
    renderPass_     = VK_NULL_HANDLE;
    framebuffer_    = VK_NULL_HANDLE;

    /* Store new record state */
    recordState_ = RecordState::OutsideRenderPass;
}

static void ToVkClearColor(VkClearColorValue& dst, const float (&src)[4])
{
    dst.float32[0] = src[0];
    dst.float32[1] = src[1];
    dst.float32[2] = src[2];
    dst.float32[3] = src[3];
}

static VkImageAspectFlags GetDepthStencilAspectMask(long flags)
{
    VkImageAspectFlags aspectMask = 0;

    if ((flags & ClearFlags::Depth) != 0)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if ((flags & ClearFlags::Stencil) != 0)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    return aspectMask;
}

void VKCommandBuffer::Clear(long flags, const ClearValue& clearValue)
{
    VkClearAttachment attachments[LLGL_MAX_NUM_ATTACHMENTS];

    std::uint32_t numAttachments = 0;

    /* Fill clear descriptors for color attachments */
    if ((flags & ClearFlags::Color) != 0)
    {
        VkClearColorValue clearColor;
        ToVkClearColor(clearColor, clearValue.color);

        numAttachments = std::min(numColorAttachments_, LLGL_MAX_NUM_COLOR_ATTACHMENTS);
        for_range(i, numAttachments)
        {
            VkClearAttachment& attachment = attachments[i];
            {
                attachment.aspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
                attachment.colorAttachment  = i;
                attachment.clearValue.color = clearColor;
            }
        }
    }

    /* Fill clear descriptor for depth-stencil attachment */
    if ((flags & ClearFlags::DepthStencil) != 0 && hasDepthStencilAttachment_)
    {
        VkClearAttachment& attachment = attachments[numAttachments++];
        {
            attachment.aspectMask                       = GetDepthStencilAspectMask(flags);
            attachment.colorAttachment                  = 0; // ignored
            attachment.clearValue.depthStencil.depth    = clearValue.depth;
            attachment.clearValue.depthStencil.stencil  = clearValue.stencil;
        }
    }

    /* Clear all framebuffer attachments */
    ClearFramebufferAttachments(numAttachments, attachments);
}

void VKCommandBuffer::ClearAttachments(std::uint32_t numAttachments, const AttachmentClear* attachments)
{
    /* Convert clear attachment descriptors */
    VkClearAttachment attachmentsVK[LLGL_MAX_NUM_ATTACHMENTS];

    std::uint32_t numAttachmentsVK = 0;

    for_range(i, std::min(numAttachments, LLGL_MAX_NUM_ATTACHMENTS))
    {
        VkClearAttachment& dst = attachmentsVK[numAttachmentsVK];
        const AttachmentClear& src = attachments[i];

        if ((src.flags & ClearFlags::Color) != 0)
        {
            /* Convert color clear command */
            dst.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
            dst.colorAttachment = src.colorAttachment;
            ToVkClearColor(dst.clearValue.color, src.clearValue.color);
            ++numAttachmentsVK;
        }
        else if (hasDepthStencilAttachment_)
        {
            /* Convert depth-stencil clear command */
            dst.aspectMask      = 0;
            dst.colorAttachment = 0;

            if ((src.flags & ClearFlags::Depth) != 0)
            {
                dst.aspectMask                      |= VK_IMAGE_ASPECT_DEPTH_BIT;
                dst.clearValue.depthStencil.depth   = src.clearValue.depth;
            }
            if ((src.flags & ClearFlags::Stencil) != 0)
            {
                dst.aspectMask                      |= VK_IMAGE_ASPECT_STENCIL_BIT;
                dst.clearValue.depthStencil.stencil = src.clearValue.stencil;
            }

            ++numAttachmentsVK;
        }
    }

    ClearFramebufferAttachments(numAttachmentsVK, attachmentsVK);
}

/* ----- Pipeline States ----- */

void VKCommandBuffer::SetPipelineState(PipelineState& pipelineState)
{
    auto& pipelineStateVK = LLGL_CAST(VKPipelineState&, pipelineState);

    /* Bind native PSO; PSOs with Y'CbCr variants are bound once the texture of their combined texture-sampler is bound */
    ResetYcbcrBindingStates();
    if (pipelineStateVK.HasYcbcrVariants())
        boundPipelineLayout_ = VK_NULL_HANDLE;
    else
    {
        boundPipelineLayout_ = pipelineStateVK.GetVkPipelineLayout();
        pipelineStateVK.BindPipelineAndStaticDescriptorSet(commandBuffer_, pipelineStateVK.GetVkPipeline(), boundPipelineLayout_);
    }

    /* Handle special case for graphics PSOs */
    pipelineBindPoint_ = pipelineStateVK.GetBindPoint();
    if (pipelineBindPoint_ == VK_PIPELINE_BIND_POINT_GRAPHICS)
    {
        auto& graphicsPSO = LLGL_CAST(VKGraphicsPSO&, pipelineStateVK);

        /* Scissor rectangle must be updated (if scissor test is disabled) */
        scissorEnabled_ = graphicsPSO.IsScissorEnabled();
        if (!scissorEnabled_ && !hasDynamicScissorRect_ && graphicsPSO.HasDynamicScissor())
        {
            /* Set scissor to render target resolution */
            vkCmdSetScissor(commandBuffer_, 0, 1, &framebufferRenderArea_);

            /* Avoid scissor update with each graphics pipeline binding (as long as render pass does not change) */
            hasDynamicScissorRect_ = true;
        }
    }

    /* Keep reference to bound piepline layout (can be null) */
    boundPipelineState_ = &pipelineStateVK;

    if (pipelineStateVK.HasYcbcrVariants())
    {
        /* Binding table of the template layout has the same descriptor indices as its variants; the descriptor cache is selected with the variant */
        boundBindingTable_  = &(pipelineStateVK.GetPipelineLayout()->GetBindingTable());
        descriptorCache_    = nullptr;
    }
    else if (pipelineStateVK.GetBindingTableAndDescriptorCache(boundBindingTable_, descriptorCache_))
    {
        if (descriptorCache_ != nullptr)
        {
            descriptorCache_->Reset();
            descriptorSetWriter_.Reset(descriptorCache_->GetNumDescriptors());
        }
    }
    else
        descriptorCache_ = nullptr;

    /* Bind automatic pipeline barrier */
    if (const VKPipelineLayout* pipelineLayoutVK = pipelineStateVK.GetPipelineLayout())
        boundPipelineBarrier_ = pipelineLayoutVK->GetAutoPipelineBarrier();
    else
        boundPipelineBarrier_ = nullptr;
}

void VKCommandBuffer::SetBlendFactor(const float color[4])
{
    vkCmdSetBlendConstants(commandBuffer_, color);
}

void VKCommandBuffer::SetStencilReference(std::uint32_t reference, const StencilFace stencilFace)
{
    vkCmdSetStencilReference(commandBuffer_, VKTypes::Map(stencilFace), reference);
}

void VKCommandBuffer::SetUniforms(std::uint32_t first, const void* data, std::uint16_t dataSize)
{
    if (boundPipelineState_ == nullptr)
        return;

    if (boundPipelineLayout_ == VK_NULL_HANDLE)
    {
        /* Push constants require a pipeline layout, so defer them until the Y'CbCr variant is resolved */
        const YcbcrPendingUniforms pending{ first, static_cast<std::uint32_t>(ycbcrPendingUniformData_.size()), dataSize };
        ycbcrPendingUniformData_.insert(ycbcrPendingUniformData_.end(), static_cast<const char*>(data), static_cast<const char*>(data) + dataSize);
        ycbcrPendingUniforms_.push_back(pending);
    }
    else
        boundPipelineState_->PushConstants(commandBuffer_, boundPipelineLayout_, first, static_cast<const char*>(data), dataSize);
}

/* ----- Queries ----- */

void VKCommandBuffer::BeginQuery(QueryHeap& queryHeap, std::uint32_t query)
{
    auto& queryHeapVK = LLGL_CAST(VKQueryHeap&, queryHeap);

    query *= queryHeapVK.GetGroupSize();

    if (queryHeapVK.GetType() == QueryType::TimeElapsed)
    {
        /* Record first timestamp */
        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryHeapVK.GetVkQueryPool(), query);
    }
    else
    {
        /* Begin query section */
        vkCmdBeginQuery(commandBuffer_, queryHeapVK.GetVkQueryPool(), query, queryHeapVK.GetControlFlags());
    }

    if (queryHeapVK.HasPredicates())
    {
        /* Mark dirty range for predicates */
        auto& predicateQueryHeapVK = LLGL_CAST(VKPredicateQueryHeap&, queryHeapVK);
        predicateQueryHeapVK.MarkDirtyRange(query, 1);
    }
}

void VKCommandBuffer::EndQuery(QueryHeap& queryHeap, std::uint32_t query)
{
    auto& queryHeapVK = LLGL_CAST(VKQueryHeap&, queryHeap);

    query *= queryHeapVK.GetGroupSize();

    if (queryHeapVK.GetType() == QueryType::TimeElapsed)
    {
        /* Record second timestamp */
        vkCmdWriteTimestamp(commandBuffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryHeapVK.GetVkQueryPool(), query + 1);
    }
    else
    {
        /* End query section */
        vkCmdEndQuery(commandBuffer_, queryHeapVK.GetVkQueryPool(), query);
    }

    #if 0//TEST
    AppendQueryPoolInFlight(&queryHeapVK);
    #endif
}

void VKCommandBuffer::BeginRenderCondition(QueryHeap& queryHeap, std::uint32_t query, const RenderConditionMode mode)
{
    LLGL_ASSERT_VK_EXT(EXT_conditional_rendering);

    auto& queryHeapVK = LLGL_CAST(VKPredicateQueryHeap&, queryHeap);

    /* Flush dirty range before using predicate result buffer */
    if (queryHeapVK.InsideDirtyRange(query, 1))
    {
        if (IsInsideRenderPass())
        {
            PauseRenderPass();
            queryHeapVK.FlushDirtyRange(commandBuffer_);
            ResumeRenderPass();
        }
        else
            queryHeapVK.FlushDirtyRange(commandBuffer_);
    }

    /* Begin conditional rendering block */
    VkConditionalRenderingBeginInfoEXT beginInfo;
    {
        beginInfo.sType     = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
        beginInfo.pNext     = nullptr;
        beginInfo.buffer    = queryHeapVK.GetResultVkBuffer();
        beginInfo.offset    = query * sizeof(std::uint32_t);
        beginInfo.flags     = (mode >= RenderConditionMode::WaitInverted ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0);
    }
    vkCmdBeginConditionalRenderingEXT(commandBuffer_, &beginInfo);
}

void VKCommandBuffer::EndRenderCondition()
{
    /* Ensure "VK_EXT_conditional_rendering" is supported */
    LLGL_ASSERT_VK_EXT(EXT_conditional_rendering);

    /* End conditional rendering block */
    vkCmdEndConditionalRenderingEXT(commandBuffer_);
}

/* ----- Stream Output ------ */

void VKCommandBuffer::BeginStreamOutput(std::uint32_t numBuffers, Buffer* const * buffers)
{
    LLGL_ASSERT_VK_EXT(EXT_transform_feedback);

    /* Get native Vulkan transform-feedback buffers */
    VkDeviceSize xfbOffsets[LLGL_MAX_NUM_SO_BUFFERS];
    VkDeviceSize xfbSizes[LLGL_MAX_NUM_SO_BUFFERS];

    xfbState_.numXfbBuffers = std::min<std::uint32_t>(numBuffers, LLGL_MAX_NUM_SO_BUFFERS);

    for_range(i, xfbState_.numXfbBuffers)
    {
        VKBuffer* bufferVK = LLGL_CAST(VKBuffer*, buffers[i]);
        xfbState_.xfbBuffers[i] = bufferVK->GetVkBuffer();
        xfbState_.xfbCounterOffsets[i] = bufferVK->GetXfbCounterOffset();
        xfbOffsets[i] = 0;
        xfbSizes[i] = bufferVK->GetSize();
    }

    /* Bind transform-feedback buffers and start recording stream-outpuits */
    vkCmdBindTransformFeedbackBuffersEXT(commandBuffer_, 0, xfbState_.numXfbBuffers, xfbState_.xfbBuffers, xfbOffsets, xfbSizes);
    vkCmdBeginTransformFeedbackEXT(commandBuffer_, 0, 0, nullptr, nullptr);
}

void VKCommandBuffer::EndStreamOutput()
{
    LLGL_ASSERT_VK_EXT(EXT_transform_feedback);

    /* End transform-feedback and specify counter buffers here to write the final counter values */
    vkCmdEndTransformFeedbackEXT(commandBuffer_, 0, xfbState_.numXfbBuffers, xfbState_.xfbBuffers, xfbState_.xfbCounterOffsets);

    /* Ensure transform-feedback counter values are accessible in subsequent DrawStreamOutput() commands */
    for_range(i, xfbState_.numXfbBuffers)
    {
        BufferPipelineBarrier(
            xfbState_.xfbBuffers[i],
            xfbState_.xfbCounterOffsets[i],
            sizeof(std::uint32_t),
            VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT,
            VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
            VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
        );
    }
}

/* ----- Drawing ----- */

void VKCommandBuffer::Draw(std::uint32_t numVertices, std::uint32_t firstVertex)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDraw(commandBuffer_, numVertices, 1, firstVertex, 0);
}

void VKCommandBuffer::DrawIndexed(std::uint32_t numIndices, std::uint32_t firstIndex)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndexed(commandBuffer_, numIndices, 1, firstIndex, 0, 0);
}

void VKCommandBuffer::DrawIndexed(std::uint32_t numIndices, std::uint32_t firstIndex, std::int32_t vertexOffset)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndexed(commandBuffer_, numIndices, 1, firstIndex, vertexOffset, 0);
}

void VKCommandBuffer::DrawInstanced(std::uint32_t numVertices, std::uint32_t firstVertex, std::uint32_t numInstances)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDraw(commandBuffer_, numVertices, numInstances, firstVertex, 0);
}

void VKCommandBuffer::DrawInstanced(std::uint32_t numVertices, std::uint32_t firstVertex, std::uint32_t numInstances, std::uint32_t firstInstance)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDraw(commandBuffer_, numVertices, numInstances, firstVertex, firstInstance);
}

void VKCommandBuffer::DrawIndexedInstanced(std::uint32_t numIndices, std::uint32_t numInstances, std::uint32_t firstIndex)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndexed(commandBuffer_, numIndices, numInstances, firstIndex, 0, 0);
}

void VKCommandBuffer::DrawIndexedInstanced(std::uint32_t numIndices, std::uint32_t numInstances, std::uint32_t firstIndex, std::int32_t vertexOffset)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndexed(commandBuffer_, numIndices, numInstances, firstIndex, vertexOffset, 0);
}

void VKCommandBuffer::DrawIndexedInstanced(std::uint32_t numIndices, std::uint32_t numInstances, std::uint32_t firstIndex, std::int32_t vertexOffset, std::uint32_t firstInstance)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndexed(commandBuffer_, numIndices, numInstances, firstIndex, vertexOffset, firstInstance);
}

void VKCommandBuffer::DrawIndirect(Buffer& buffer, std::uint64_t offset)
{
    if (!PrepareDrawOrDispatch())
        return;
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    vkCmdDrawIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, 1, 0);
}

void VKCommandBuffer::DrawIndirect(Buffer& buffer, std::uint64_t offset, std::uint32_t numCommands, std::uint32_t stride)
{
    if (!PrepareDrawOrDispatch())
        return;
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    if (maxDrawIndirectCount_ < numCommands)
    {
        /* Encode multiple indirect draw commands if limit is exceeded */
        while (numCommands > 0)
        {
            const std::uint32_t drawCount = (numCommands % maxDrawIndirectCount_);
            vkCmdDrawIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, drawCount, stride);
            numCommands -= drawCount;
            offset += stride;
        }
    }
    else
        vkCmdDrawIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, numCommands, stride);
}

void VKCommandBuffer::DrawIndexedIndirect(Buffer& buffer, std::uint64_t offset)
{
    if (!PrepareDrawOrDispatch())
        return;
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    vkCmdDrawIndexedIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, 1, 0);
}

void VKCommandBuffer::DrawIndexedIndirect(Buffer& buffer, std::uint64_t offset, std::uint32_t numCommands, std::uint32_t stride)
{
    if (!PrepareDrawOrDispatch())
        return;
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    if (maxDrawIndirectCount_ < numCommands)
    {
        /* Encode multiple indirect draw commands if limit is exceeded */
        while (numCommands > 0)
        {
            const std::uint32_t drawCount = (numCommands % maxDrawIndirectCount_);
            vkCmdDrawIndexedIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, drawCount, 0);
            numCommands -= drawCount;
            offset += stride;
        }
    }
    else
        vkCmdDrawIndexedIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset, numCommands, stride);
}

void VKCommandBuffer::DrawStreamOutput()
{
    LLGL_ASSERT_VK_EXT(EXT_transform_feedback);
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDrawIndirectByteCountEXT(commandBuffer_, 1, 0, iaState_.ia0XfbCounterBuffer, iaState_.ia0XfbCounterBufferOffset, 0, iaState_.ia0VertexStride);
}

/* ----- Compute ----- */

void VKCommandBuffer::Dispatch(std::uint32_t numWorkGroupsX, std::uint32_t numWorkGroupsY, std::uint32_t numWorkGroupsZ)
{
    if (!PrepareDrawOrDispatch())
        return;
    vkCmdDispatch(commandBuffer_, numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
}

void VKCommandBuffer::DispatchIndirect(Buffer& buffer, std::uint64_t offset)
{
    if (!PrepareDrawOrDispatch())
        return;
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    vkCmdDispatchIndirect(commandBuffer_, bufferVK.GetVkBuffer(), offset);
}

/* ----- Debugging ----- */

void VKCommandBuffer::PushDebugGroup(const char* name)
{
    if (HasExtension(VKExt::EXT_debug_marker))
    {
        VkDebugMarkerMarkerInfoEXT markerInfo;
        {
            markerInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
            markerInfo.pNext        = nullptr;
            markerInfo.pMarkerName  = name;
            markerInfo.color[0]     = 0.0f;
            markerInfo.color[1]     = 0.0f;
            markerInfo.color[2]     = 0.0f;
            markerInfo.color[3]     = 0.0f;
        }
        vkCmdDebugMarkerBeginEXT(commandBuffer_, &markerInfo);
    }
}

void VKCommandBuffer::PopDebugGroup()
{
    if (HasExtension(VKExt::EXT_debug_marker))
        vkCmdDebugMarkerEndEXT(commandBuffer_);
}

/* ----- Extensions ----- */

void VKCommandBuffer::DoNativeCommand(const void* nativeCommand, std::size_t nativeCommandSize)
{
    // dummy
}

bool VKCommandBuffer::GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize)
{
    if (nativeHandle != nullptr && nativeHandleSize == sizeof(Vulkan::CommandBufferNativeHandle))
    {
        auto* nativeHandleVK = static_cast<Vulkan::CommandBufferNativeHandle*>(nativeHandle);
        nativeHandleVK->commandBuffer = commandBuffer_;
        return true;
    }
    return false;
}


/*
 * ======= Private: =======
 */

void VKCommandBuffer::CreateVkCommandPool(std::uint32_t queueFamilyIndex)
{
    /* Create command pool */
    VkCommandPoolCreateInfo createInfo;
    {
        createInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.pNext            = nullptr;
        createInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = queueFamilyIndex;
    }
    VkResult result = vkCreateCommandPool(device_, &createInfo, nullptr, commandPool_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan command pool");
}

void VKCommandBuffer::CreateVkCommandBuffers()
{
    /* Allocate command buffers */
    VkCommandBufferAllocateInfo allocInfo;
    {
        allocInfo.sType                 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.pNext                 = nullptr;
        allocInfo.commandPool           = commandPool_;
        allocInfo.level                 = bufferLevel_;
        allocInfo.commandBufferCount    = numCommandBuffers_;
    }
    VkResult result = vkAllocateCommandBuffers(device_, &allocInfo, commandBufferArray_);
    VKThrowIfFailed(result, "failed to allocate Vulkan command buffers");
}

void VKCommandBuffer::CreateVkRecordingFences()
{
    /* Create all recording fences with their initial state being signaled */
    VkFenceCreateInfo createInfo;
    {
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }

    for_range(i, numCommandBuffers_)
    {
        /* Create fence for command buffer recording */
        VkResult result = vkCreateFence(device_, &createInfo, nullptr, recordingFenceArray_[i].ReleaseAndGetAddressOf());
        VKThrowIfFailed(result, "failed to create Vulkan fence");
    }
}

void VKCommandBuffer::CreateStagingBufferPools(VKDeviceMemoryManager& deviceMemoryMngr, VkDeviceSize minStagingPoolSize)
{
    /* Create command allocators and descriptor heap pools */
    constexpr VkDeviceSize minStagingChunkSize = 256;
    minStagingPoolSize = std::max(minStagingChunkSize, minStagingPoolSize);

    for_range(i, numCommandBuffers_)
        stagingBufferPools_[i].InitializeDevice(&deviceMemoryMngr, minStagingPoolSize);
}

void VKCommandBuffer::ClearFramebufferAttachments(std::uint32_t numAttachments, const VkClearAttachment* attachments)
{
    if (numAttachments > 0)
    {
        /* Clear framebuffer attachments at the entire image region */
        VkClearRect clearRect;
        {
            clearRect.rect              = framebufferRenderArea_;
            clearRect.baseArrayLayer    = 0;
            clearRect.layerCount        = 1;
        }
        vkCmdClearAttachments(commandBuffer_, numAttachments, attachments, 1, &clearRect);
    }
}

static void ToVkClearDepthStencil(VkClearDepthStencilValue& dst, float srcDepth, std::uint32_t srcStencil)
{
    dst.depth   = srcDepth;
    dst.stencil = srcStencil;
}

void VKCommandBuffer::ConvertRenderPassClearValues(
    const VKRenderPass& renderPass,
    std::uint32_t&      dstClearValuesCount,
    VkClearValue*       dstClearValues,
    std::uint32_t       srcClearValuesCount,
    const ClearValue*   srcClearValues)
{
    /* Fill array of clear values */
    dstClearValuesCount     = renderPass.GetNumClearValues();

    std::uint64_t   clearValuesMask     = renderPass.GetClearValuesMask();
    std::uint8_t    depthStencilIndex   = renderPass.GetDepthStencilIndex();
    const bool      hasMultiSampling    = (renderPass.GetSampleCountBits() > VK_SAMPLE_COUNT_1_BIT);

    const VkClearColorValue         defaultClearColor           = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    const VkClearDepthStencilValue  defaultClearDepthStencil    = { 1.0f, 0 };

    std::uint32_t srcIndex = 0;

    for_range(i, dstClearValuesCount)
    {
        /* Check if current attachment index requires a clear value */
        if (((clearValuesMask >> i) & 0x1ull) != 0)
        {
            /* Select destination Vulkan clear value */
            VkClearValue& dst = dstClearValues[i];

            if (srcIndex < srcClearValuesCount)
            {
                /* Set specified clear parameter */
                const ClearValue& src = srcClearValues[srcIndex++];
                if (i == depthStencilIndex)
                    ToVkClearDepthStencil(dst.depthStencil, src.depth, src.stencil);
                else
                    ToVkClearColor(dst.color, src.color);
            }
            else
            {
                /* Set default clear parameters */
                if (i == depthStencilIndex)
                    dst.depthStencil = defaultClearDepthStencil;
                else
                    dst.color = defaultClearColor;
            }
        }
    }

    if (hasMultiSampling)
        dstClearValuesCount += renderPass.GetNumColorAttachments();
}

void VKCommandBuffer::PauseRenderPass()
{
    vkCmdEndRenderPass(commandBuffer_);
}

void VKCommandBuffer::ResumeRenderPass()
{
    /* Record begin of render pass */
    VkRenderPassBeginInfo beginInfo;
    {
        beginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.pNext             = nullptr;
        beginInfo.renderPass        = secondaryRenderPass_;
        beginInfo.framebuffer       = framebuffer_;
        beginInfo.renderArea        = framebufferRenderArea_;
        beginInfo.clearValueCount   = 0;
        beginInfo.pClearValues      = nullptr;
    }
    vkCmdBeginRenderPass(commandBuffer_, &beginInfo, subpassContents_);
}

bool VKCommandBuffer::IsInsideRenderPass() const
{
    return (recordState_ == RecordState::InsideRenderPass);
}

void VKCommandBuffer::BufferPipelineBarrier(
    VkBuffer                buffer,
    VkDeviceSize            offset,
    VkDeviceSize            size,
    VkAccessFlags           srcAccessMask,
    VkAccessFlags           dstAccessMask,
    VkPipelineStageFlags    srcStageMask,
    VkPipelineStageFlags    dstStageMask)
{
    VkBufferMemoryBarrier barrier;
    {
        barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.pNext               = nullptr;
        barrier.srcAccessMask       = srcAccessMask;
        barrier.dstAccessMask       = dstAccessMask;
        barrier.srcQueueFamilyIndex = 0;
        barrier.dstQueueFamilyIndex = 0;
        barrier.buffer              = buffer;
        barrier.offset              = offset;
        barrier.size                = size;
    }
    vkCmdPipelineBarrier(commandBuffer_, srcStageMask, dstStageMask, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void VKCommandBuffer::FlushDescriptorCache()
{
    if (descriptorCache_ != nullptr && descriptorCache_->IsInvalidated())
    {
        VkDescriptorSet descriptorSet = descriptorCache_->FlushDescriptorSet(*descriptorSetPool_, descriptorSetWriter_);
        boundPipelineState_->BindDynamicDescriptorSet(commandBuffer_, boundPipelineLayout_, descriptorSet);
    }
}

void VKCommandBuffer::SubmitAutoPipelineBarrier()
{
    if (boundPipelineBarrier_ != nullptr)
        boundPipelineBarrier_->Submit(commandBuffer_);
}

bool VKCommandBuffer::PrepareDrawOrDispatch()
{
    /* PSOs with Y'CbCr variants have no native pipeline until the texture of their combined texture-sampler is bound */
    if (boundPipelineState_ != nullptr && boundPipelineLayout_ == VK_NULL_HANDLE)
    {
        Log::Errorf("cannot draw or dispatch with Vulkan PSO before a texture with Y'CbCr conversion has been bound to its combined texture-sampler\n");
        return false;
    }
    FlushDescriptorCache();
    SubmitAutoPipelineBarrier();
    return true;
}

bool VKCommandBuffer::SetYcbcrPipelineResource(std::uint32_t descriptor, Resource& resource)
{
    const VKPipelineLayout* pipelineLayoutVK = boundPipelineState_->GetPipelineLayout();

    /* The sampler is derived from the texture's conversion, so the sampler binding is only validated */
    if (descriptor == pipelineLayoutVK->GetYcbcrSamplerDescriptor())
    {
        #ifdef LLGL_DEBUG
        if (resource.GetResourceType() == ResourceType::Sampler && boundYcbcrVariant_ != nullptr)
        {
            VKSampler& samplerVK = LLGL_CAST(VKSampler&, resource);
            if (samplerVK.GetYcbcrConversion() != boundYcbcrVariant_->conversion.get())
                Log::Errorf("mismatch between Y'CbCr conversion of sampler and texture of combined texture-sampler\n");
        }
        #endif
        return false;
    }

    RecordYcbcrBoundResource(descriptor, resource);

    if (descriptor == pipelineLayoutVK->GetYcbcrTextureDescriptor())
    {
        if (resource.GetResourceType() != ResourceType::Texture)
            return false;

        const VKYcbcrConversionSPtr& conversion = LLGL_CAST(VKTexture&, resource).GetYcbcrConversionSPtr();
        if (!conversion)
        {
            Log::Errorf("cannot bind texture without Y'CbCr conversion to combined texture-sampler with 'LLGL::BindFlags::SamplerYcbcrConversion'\n");
            return false;
        }

        /* Switch pipeline variant if the conversion has changed; this writes all recorded resources including this texture */
        if (boundYcbcrVariant_ == nullptr || boundYcbcrVariant_->conversion != conversion)
        {
            if (const VKYcbcrPipelineVariant* variant = boundPipelineState_->GetOrCreateYcbcrVariant(conversion))
                BindYcbcrPipelineVariant(*variant);
            return false;
        }
    }

    /* Write resource to the descriptor cache of the current variant; otherwise, it is written once the variant is resolved */
    return (boundYcbcrVariant_ != nullptr);
}

void VKCommandBuffer::BindYcbcrPipelineVariant(const VKYcbcrPipelineVariant& variant)
{
    /* Bind native pipeline of variant */
    boundYcbcrVariant_      = &variant;
    boundPipelineLayout_    = variant.layoutPermutation->GetVkPipelineLayout();
    boundPipelineState_->BindPipelineAndStaticDescriptorSet(commandBuffer_, variant.pipeline.Get(), boundPipelineLayout_);

    /* Switch to descriptor cache of variant; pending writes of the previous cache are discarded */
    boundBindingTable_  = &(variant.layoutPermutation->GetBindingTable());
    descriptorCache_    = variant.layoutPermutation->GetDescriptorCache();
    LLGL_ASSERT_PTR(descriptorCache_);
    descriptorCache_->Reset();
    descriptorSetWriter_.Reset(descriptorCache_->GetNumDescriptors());

    /* Bind heap descriptor set and push constants that were specified before the variant was resolved */
    if (ycbcrHeapDescriptorSet_ != VK_NULL_HANDLE)
        boundPipelineState_->BindHeapDescriptorSet(commandBuffer_, boundPipelineLayout_, ycbcrHeapDescriptorSet_);

    for (const YcbcrPendingUniforms& pending : ycbcrPendingUniforms_)
        boundPipelineState_->PushConstants(commandBuffer_, boundPipelineLayout_, pending.first, &(ycbcrPendingUniformData_[pending.offset]), pending.size);

    ycbcrPendingUniforms_.clear();
    ycbcrPendingUniformData_.clear();

    /* Write all resources that were bound since SetPipelineState to the new descriptor cache */
    for (const YcbcrBoundResource& boundResource : ycbcrBoundResources_)
    {
        const VKLayoutBinding& binding = boundBindingTable_->dynamicBindings[boundResource.descriptor];
        if (binding.descriptorType != VK_DESCRIPTOR_TYPE_MAX_ENUM)
            descriptorCache_->EmplaceDescriptor(*boundResource.resource, binding, descriptorSetWriter_);
    }
}

void VKCommandBuffer::RecordYcbcrBoundResource(std::uint32_t descriptor, Resource& resource)
{
    for (YcbcrBoundResource& boundResource : ycbcrBoundResources_)
    {
        if (boundResource.descriptor == descriptor)
        {
            boundResource.resource = &resource;
            return;
        }
    }
    ycbcrBoundResources_.push_back(YcbcrBoundResource{ descriptor, &resource });
}

void VKCommandBuffer::ResetYcbcrBindingStates()
{
    boundYcbcrVariant_      = nullptr;
    ycbcrHeapDescriptorSet_ = VK_NULL_HANDLE;
    ycbcrBoundResources_.clear();
    ycbcrPendingUniforms_.clear();
    ycbcrPendingUniformData_.clear();
}

void VKCommandBuffer::TrackExternalTexture(VKTexture& textureVK)
{
    if (std::find(externalTextures_.begin(), externalTextures_.end(), &textureVK) == externalTextures_.end())
        externalTextures_.push_back(&textureVK);
}

// Initializes an image barrier for a queue family ownership transfer of the specified external texture.
static void InitExternalTextureBarrier(
    VkImageMemoryBarrier&   barrier,
    VKTexture&              textureVK,
    VkAccessFlags           srcAccessMask,
    VkAccessFlags           dstAccessMask,
    VkImageLayout           oldLayout,
    VkImageLayout           newLayout,
    std::uint32_t           srcQueueFamilyIndex,
    std::uint32_t           dstQueueFamilyIndex)
{
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext                           = nullptr;
    barrier.srcAccessMask                   = srcAccessMask;
    barrier.dstAccessMask                   = dstAccessMask;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = srcQueueFamilyIndex;
    barrier.dstQueueFamilyIndex             = dstQueueFamilyIndex;
    barrier.image                           = textureVK.GetVkImage();
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
}

void VKCommandBuffer::RecordExternalTextureReleaseBarriers()
{
    /* Release ownership to the external producer; this is always recorded outside of a render pass, since End() is called after EndRenderPass() */
    SmallVector<VkImageMemoryBarrier, 4> barriers;
    barriers.resize(externalTextures_.size());

    for_range(i, externalTextures_.size())
    {
        InitExternalTextureBarrier(
            barriers[i],
            *externalTextures_[i],
            VK_ACCESS_SHADER_READ_BIT,
            0, // Ignored for release operations
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            queueGraphicsFamily_,
            GetExternalQueueFamilyIndex()
        );
    }

    vkCmdPipelineBarrier(
        commandBuffer_,
        g_externalTextureStageMask,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<std::uint32_t>(barriers.size()), barriers.data()
    );
}

void VKCommandBuffer::RecordExternalTextureAcquirePrologue()
{
    /* Allocate prologue command buffer on demand; it shares the recording fence with the native command buffer of the same index */
    VkCommandBuffer& prologue = prologueBufferArray_[commandBufferIndex_];
    if (prologue == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo allocInfo;
        {
            allocInfo.sType                 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.pNext                 = nullptr;
            allocInfo.commandPool           = commandPool_;
            allocInfo.level                 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount    = 1;
        }
        VkResult result = vkAllocateCommandBuffers(device_, &allocInfo, &prologue);
        VKThrowIfFailed(result, "failed to allocate Vulkan command buffer for external texture prologue");
    }

    VkCommandBufferBeginInfo beginInfo;
    {
        beginInfo.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext             = nullptr;
        beginInfo.flags             = (usageFlags_ & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        beginInfo.pInheritanceInfo  = nullptr;
    }
    VkResult result = vkBeginCommandBuffer(prologue, &beginInfo);
    VKThrowIfFailed(result, "failed to begin Vulkan command buffer for external texture prologue");

    /* Acquire ownership from the external producer; the old layout must not be UNDEFINED, since that would allow the implementation to discard the content */
    SmallVector<VkImageMemoryBarrier, 4> barriers;
    barriers.resize(externalTextures_.size());

    for_range(i, externalTextures_.size())
    {
        InitExternalTextureBarrier(
            barriers[i],
            *externalTextures_[i],
            0, // Ignored for acquire operations
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            GetExternalQueueFamilyIndex(),
            queueGraphicsFamily_
        );
    }

    vkCmdPipelineBarrier(
        prologue,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        g_externalTextureStageMask,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<std::uint32_t>(barriers.size()), barriers.data()
    );

    result = vkEndCommandBuffer(prologue);
    VKThrowIfFailed(result, "failed to end Vulkan command buffer for external texture prologue");

    prologueRecorded_[commandBufferIndex_] = true;
}

void VKCommandBuffer::AcquireNextBuffer()
{
    /* Move to next command buffer index */
    commandBufferIndex_ = (commandBufferIndex_ + 1) % numCommandBuffers_;

    /* Wait for fence before using next command buffer */
    recordingFence_ = recordingFenceArray_[commandBufferIndex_].Get();
    if (recordingFenceDirty_[commandBufferIndex_])
        vkWaitForFences(device_, 1, &recordingFence_, VK_TRUE, UINT64_MAX);

    /* Reset fence state after it has been signaled by the command queue */
    vkResetFences(device_, 1, &recordingFence_);
    recordingFenceDirty_[commandBufferIndex_] = false;

    /* Make next command buffer current and reset pools and context */
    commandBuffer_      = commandBufferArray_[commandBufferIndex_];
    descriptorSetPool_  = &(descriptorSetPoolArray_[commandBufferIndex_]);
    descriptorSetPool_->Reset();
    context_.Reset(commandBuffer_);

    stagingBufferPools_[commandBufferIndex_].Reset();

    /* Prologue of this native command buffer is recorded again if external textures are bound */
    prologueRecorded_[commandBufferIndex_] = false;
    externalTextures_.clear();
}

std::uint32_t VKCommandBuffer::GetExternalQueueFamilyIndex() const
{
    #if VK_EXT_queue_family_foreign
    if (HasExtension(VKExt::EXT_queue_family_foreign))
        return VK_QUEUE_FAMILY_FOREIGN_EXT;
    #endif
    return VK_QUEUE_FAMILY_EXTERNAL;
}

void VKCommandBuffer::ResetBindingStates()
{
    boundSwapChain_         = nullptr;
    boundBindingTable_      = nullptr;
    boundPipelineState_     = nullptr;
    boundPipelineBarrier_   = nullptr;
    boundPipelineLayout_    = VK_NULL_HANDLE;
    descriptorCache_        = nullptr;
    ResetYcbcrBindingStates();
}

#if 0
void VKCommandBuffer::ResetQueryPoolsInFlight()
{
    for_range(i, numQueryHeapsInFlight_)
    {
        auto& queryHeapVK = *(queryHeapsInFlight_[0]);
        vkCmdResetQueryPool(
            commandBuffer_,
            queryHeapVK.GetVkQueryPool(),
            0,
            queryHeapVK.GetNumQueries()
        );
    }
    numQueryHeapsInFlight_ = 0;
}

void VKCommandBuffer::AppendQueryPoolInFlight(VKQueryHeap* queryHeap)
{
    if (numQueryHeapsInFlight_ >= queryHeapsInFlight_.size())
        queryHeapsInFlight_.push_back(queryHeap);
    else
        queryHeapsInFlight_[numQueryHeapsInFlight_] = queryHeap;
    ++numQueryHeapsInFlight_;
}
#endif

std::uint32_t VKCommandBuffer::GetNumVkCommandBuffers(const CommandBufferDescriptor& desc)
{
    constexpr std::uint32_t numNativeBuffersDefault = 2;
    if (desc.numNativeBuffers == 0)
        return numNativeBuffersDefault;
    else
        return Clamp<std::uint32_t>(desc.numNativeBuffers, 1u, VKCommandBuffer::maxNumCommandBuffers);
}


} // /namespace LLGL



// ================================================================================
