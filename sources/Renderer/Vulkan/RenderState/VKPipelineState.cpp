/*
 * VKPipelineState.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKPipelineState.h"
#include "VKPipelineLayout.h"
#include "VKPipelineLayoutPermutationPool.h"
#include "../Shader/VKShader.h"
#include "../Shader/VKShaderModulePool.h"
#include "../../CheckedCast.h"
#include "../../../Core/CoreUtils.h"


namespace LLGL
{


VKYcbcrPipelineVariant::VKYcbcrPipelineVariant(VkDevice device) :
    pipeline { device, vkDestroyPipeline }
{
}

VKYcbcrPipelineVariant::~VKYcbcrPipelineVariant()
{
    /* Release layout permutation before the conversion, since the permutation refers to the conversion's canonical sampler */
    VKPipelineLayoutPermutationPool::Get().ReleasePermutation(std::move(layoutPermutation));
}

VKPipelineState::VKPipelineState(
    VkDevice                    device,
    VkPipelineBindPoint         bindPoint,
    const ArrayView<Shader*>&   shaders,
    const PipelineLayout*       pipelineLayout)
:
    device_    { device                    },
    pipeline_  { device, vkDestroyPipeline },
    bindPoint_ { bindPoint                 }
{
    if (pipelineLayout != nullptr)
    {
        pipelineLayout_ = LLGL_CAST(const VKPipelineLayout*, pipelineLayout);
        if (pipelineLayout_->HasYcbcrBinding())
        {
            /*
            Layout permutations are created per Y'CbCr conversion when a texture is bound (see GetOrCreateYcbcrVariant),
            so only store the parameters that are shared between all variants, i.e. push constants and texel buffers.
            */
            hasYcbcrVariants_ = true;
            if (!pipelineLayout_->BuildPermutationParams(shaders, ycbcrBaseParams_, uniformRanges_))
                pipelineLayout_->GetDefaultPermutationParams(ycbcrBaseParams_);
        }
        else if (pipelineLayout_->CanHaveLayoutPermutations())
            pipelineLayoutPerm_ = pipelineLayout_->CreatePermutation(device, shaders, uniformRanges_);
    }
}

VKPipelineState::~VKPipelineState()
{
    ycbcrVariants_.clear();
    VKPipelineLayoutPermutationPool::Get().ReleasePermutation(std::move(pipelineLayoutPerm_));
}

const Report* VKPipelineState::GetReport() const
{
    return (*report_.GetText() != '\0' || report_.HasErrors() ? &report_ : nullptr);
}

void VKPipelineState::BindPipelineAndStaticDescriptorSet(VkCommandBuffer commandBuffer, VkPipeline pipeline, VkPipelineLayout layout)
{
    vkCmdBindPipeline(commandBuffer, GetBindPoint(), pipeline);

    if (pipelineLayout_ != nullptr)
    {
        VkDescriptorSet staticDescriptorSet = pipelineLayout_->GetStaticDescriptorSet();
        if (staticDescriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(
                /*commandBuffer:*/      commandBuffer,
                /*pipelineBindPoint:*/  GetBindPoint(),
                /*layout:*/             layout,
                /*firstSet:*/           pipelineLayout_->GetBindPointForImmutableSamplers(),
                /*descriptorSetCount:*/ 1,
                /*pDescriptorSets:*/    &staticDescriptorSet,
                /*dynamicOffsetCount:*/ 0,
                /*pDynamicOffsets*/     nullptr
            );
        }
    }
}

//private
void VKPipelineState::BindDescriptorSets(
    VkCommandBuffer         commandBuffer,
    VkPipelineLayout        layout,
    std::uint32_t           firstSet,
    std::uint32_t           descriptorSetCount,
    const VkDescriptorSet*  descriptorSets)
{
    vkCmdBindDescriptorSets(
        /*commandBuffer:*/      commandBuffer,
        /*pipelineBindPoint:*/  GetBindPoint(),
        /*layout:*/             layout,
        /*firstSet:*/           firstSet,
        /*descriptorSetCount:*/ descriptorSetCount,
        /*pDescriptorSets:*/    descriptorSets,
        /*dynamicOffsetCount:*/ 0,
        /*pDynamicOffsets*/     nullptr
    );
}

void VKPipelineState::BindDynamicDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkDescriptorSet descriptorSet)
{
    if (pipelineLayout_ != nullptr && descriptorSet != VK_NULL_HANDLE)
        BindDescriptorSets(commandBuffer, layout, pipelineLayout_->GetBindPointForDynamicBindings(), 1, &descriptorSet);
}

void VKPipelineState::BindHeapDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkDescriptorSet descriptorSet)
{
    if (pipelineLayout_ != nullptr && descriptorSet != VK_NULL_HANDLE)
        BindDescriptorSets(commandBuffer, layout, pipelineLayout_->GetBindPointForHeapBindings(), 1, &descriptorSet);
}

void VKPipelineState::PushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, std::uint32_t first, const char* data, std::uint32_t size)
{
    if (first >= uniformRanges_.size())
        return /*OutOfBounds*/;

    const char* pendingData = data;
    VkPushConstantRange pendingRange = {};

    auto FlushPushConstants = [&pendingRange, &pendingData, layout, commandBuffer]()
    {
        if (pendingRange.size > 0)
        {
            vkCmdPushConstants(
                commandBuffer,
                layout,
                pendingRange.stageFlags,
                pendingRange.offset,
                pendingRange.size,
                pendingData
            );
            pendingData += pendingRange.size;
            pendingRange.size = 0;
        }
    };

    for (const std::uint32_t end = static_cast<std::uint32_t>(uniformRanges_.size()); first < end; ++first)
    {
        /* Stop once we reached end of input data */
        const VkPushConstantRange& currentRange = uniformRanges_[first];
        if (size < currentRange.size)
            break;

        if (currentRange.offset > pendingRange.offset + pendingRange.size || currentRange.stageFlags != pendingRange.stageFlags)
        {
            FlushPushConstants();
            pendingRange.stageFlags = currentRange.stageFlags;
            pendingRange.offset     = currentRange.offset;
        }

        pendingRange.size   += currentRange.size;
        size                -= currentRange.size;
    }

    FlushPushConstants();
}

const VKYcbcrPipelineVariant* VKPipelineState::GetOrCreateYcbcrVariant(const VKYcbcrConversionSPtr& conversion)
{
    if (!hasYcbcrVariants_ || !conversion)
        return nullptr;

    /* Variants can be requested by multiple command buffers that are recorded in parallel */
    std::lock_guard<std::mutex> guard{ ycbcrVariantsMutex_ };

    /* Conversions are shared by the pool for equal descriptors, so they can be compared by pointer */
    for (const auto& variant : ycbcrVariants_)
    {
        if (variant->conversion == conversion)
            return variant.get();
    }

    /* Create layout permutation with canonical sampler of this conversion and a native pipeline for it */
    auto variant = MakeUnique<VKYcbcrPipelineVariant>(device_);
    {
        variant->conversion         = conversion;
        variant->layoutPermutation  = pipelineLayout_->CreateYcbcrPermutation(device_, ycbcrBaseParams_, *conversion);
    }
    if (!CreateVkPipelineVariant(variant->layoutPermutation->GetVkPipelineLayout(), variant->pipeline))
        return nullptr;

    ycbcrVariants_.push_back(std::move(variant));
    return ycbcrVariants_.back().get();
}

bool VKPipelineState::GetBindingTableAndDescriptorCache(const VKLayoutBindingTable*& outBindingTable, VKDescriptorCache*& outDescriptorCache) const
{
    if (pipelineLayoutPerm_.get() != nullptr)
    {
        outBindingTable     = &(pipelineLayoutPerm_->GetBindingTable());
        outDescriptorCache  = pipelineLayoutPerm_->GetDescriptorCache();
        return true;
    }
    if (pipelineLayout_ != nullptr)
    {
        outBindingTable     = &(pipelineLayout_->GetBindingTable());
        outDescriptorCache  = pipelineLayout_->GetDescriptorCache();
        return true;
    }
    return false;
}


/*
 * ======= Protected: =======
 */

VkPipeline* VKPipelineState::ReleaseAndGetAddressOfVkPipeline()
{
    return pipeline_.ReleaseAndGetAddressOf();
}

VkPipelineLayout VKPipelineState::GetVkPipelineLayout() const
{
    if (pipelineLayoutPerm_.get())
        return pipelineLayoutPerm_->GetVkPipelineLayout();
    if (pipelineLayout_ != nullptr)
        return pipelineLayout_->GetVkPipelineLayout();
    return VKPipelineLayout::GetDefault();
}

void VKPipelineState::GetShaderCreateInfoAndOptionalPermutation(
    VKShader&                           shaderVK,
    VkPipelineShaderStageCreateInfo&    outCreateInfo,
    VKPtr<VkShaderModule>*              outOwnedShaderModule)
{
    shaderVK.FillShaderStageCreateInfo(outCreateInfo);
    const bool needsPermutation = (pipelineLayout_ != nullptr && pipelineLayout_->NeedsShaderModulePermutation(shaderVK));
    if (outOwnedShaderModule != nullptr)
    {
        /* Create a shader module owned by this PSO, since shader modules of the pool are released together with their shader */
        if (needsPermutation)
            *outOwnedShaderModule = pipelineLayout_->CreateVkShaderModulePermutation(shaderVK);
        if (outOwnedShaderModule->Get() == VK_NULL_HANDLE)
            *outOwnedShaderModule = shaderVK.CreateVkShaderModuleCopy();
        outCreateInfo.module = outOwnedShaderModule->Get();
    }
    else if (needsPermutation)
        outCreateInfo.module = VKShaderModulePool::Get().GetOrCreateVkShaderModulePermutation(shaderVK, *pipelineLayout_);
}

bool VKPipelineState::CreateVkPipelineVariant(VkPipelineLayout /*pipelineLayout*/, VKPtr<VkPipeline>& /*outPipeline*/)
{
    return false; // Y'CbCr variants are not supported by default
}


} // /namespace LLGL



// ================================================================================
