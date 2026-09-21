/*
 * VKComputePSO.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKComputePSO.h"
#include "VKPipelineCache.h"
#include "../Shader/VKShader.h"
#include "../VKTypes.h"
#include "../VKCore.h"
#include "../../CheckedCast.h"
#include "../../PipelineStateUtils.h"
#include "../../../Core/StringUtils.h"
#include <LLGL/PipelineStateFlags.h>
#include <cstddef>


namespace LLGL
{


VKComputePSO::VKComputePSO(
    VkDevice                            device,
    const ComputePipelineDescriptor&    desc,
    PipelineCache*                      pipelineCache)
:
    VKPipelineState { device, VK_PIPELINE_BIND_POINT_COMPUTE, GetShadersAsArray(desc), desc.pipelineLayout }
{
    /* Create Vulkan compute pipeline object */
    if (VKPipelineCache* pipelineCacheVK = (pipelineCache != nullptr ? LLGL_CAST(VKPipelineCache*, pipelineCache) : nullptr))
        CreateVkPipeline(device, desc, pipelineCacheVK->GetNative());
    else
        CreateVkPipeline(device, desc);
}


/*
 * ======= Private: =======
 */

bool VKComputePSO::CreateVkPipeline(
    VkDevice                            device,
    const ComputePipelineDescriptor&    desc,
    VkPipelineCache                     pipelineCache)
{
    /* Get compute shader */
    VKShader* computeShaderVK = LLGL_CAST(VKShader*, desc.computeShader);
    if (computeShaderVK == nullptr)
    {
        GetMutableReport().Errorf("cannot create Vulkan compute pipeline without compute shader\n");
        return false;
    }

    const Report* computeShaderReport = computeShaderVK->GetReport();
    if (computeShaderReport != nullptr && computeShaderReport->HasErrors())
    {
        GetMutableReport().Errorf("Failed to load compute shader into Vulkan compute pipeline state [%s]\n", GetOptionalDebugName(desc.debugName));
        return false;
    }

    /* Get shader stages; pipeline variants need their own shader module, since they are created after the shader might have been released */
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo;
    GetShaderCreateInfoAndOptionalPermutation(*computeShaderVK, shaderStageCreateInfo, (HasYcbcrVariants() ? &variantShaderModule_ : nullptr));

    /* Create graphics pipeline state object */
    VkComputePipelineCreateInfo createInfo;
    {
        createInfo.sType                = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        createInfo.pNext                = nullptr;
        createInfo.flags                = 0;
        createInfo.stage                = shaderStageCreateInfo;
        createInfo.layout               = GetVkPipelineLayout();
        createInfo.basePipelineHandle   = VK_NULL_HANDLE;
        createInfo.basePipelineIndex    = 0;
    }

    if (HasYcbcrVariants())
    {
        /* Keep create info for pipeline variants that are created when a texture with Y'CbCr conversion is bound */
        variantEntryPoint_              = createInfo.stage.pName;
        variantCreateInfo_              = createInfo;
        variantCreateInfo_.stage.pName  = variantEntryPoint_.c_str();
        return true;
    }

    VkResult result = vkCreateComputePipelines(device, pipelineCache, 1, &createInfo, nullptr, ReleaseAndGetAddressOfVkPipeline());
    VKThrowIfFailed(result, "failed to create Vulkan compute pipeline");

    return true;
}

bool VKComputePSO::CreateVkPipelineVariant(VkPipelineLayout pipelineLayout, VKPtr<VkPipeline>& outPipeline)
{
    if (variantShaderModule_.Get() == VK_NULL_HANDLE)
        return false;

    VkComputePipelineCreateInfo createInfo = variantCreateInfo_;
    createInfo.layout = pipelineLayout;

    VkResult result = vkCreateComputePipelines(GetVkDevice(), VK_NULL_HANDLE, 1, &createInfo, nullptr, outPipeline.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan compute pipeline variant for Y'CbCr conversion");

    return true;
}


} // /namespace LLGL



// ================================================================================
