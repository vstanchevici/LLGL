/*
 * VKGraphicsPSO.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "VKGraphicsPSO.h"
#include "VKPipelineLayout.h"
#include "VKRenderPass.h"
#include "VKPipelineCache.h"
#include "../Ext/VKExtensionRegistry.h"
#include "../Shader/VKShader.h"
#include "../VKTypes.h"
#include "../VKCore.h"
#include "../../CheckedCast.h"
#include "../../PipelineStateUtils.h"
#include "../../../Core/CoreUtils.h"
#include <cstddef>
#include <LLGL/PipelineStateFlags.h>
#include <LLGL/Utils/ForRange.h>
#include <LLGL/Utils/TypeNames.h>
#include <LLGL/Container/SmallVector.h>
#include "../../../Core/Assertion.h"
#include "../../../Core/StringUtils.h"


namespace LLGL
{


VKGraphicsPSO::VKGraphicsPSO(
    VkDevice                            device,
    const RenderPass*                   defaultRenderPass,
    const GraphicsPipelineDescriptor&   desc,
    const VKGraphicsPipelineLimits&     limits,
    PipelineCache*                      pipelineCache)
:
    VKPipelineState    { device, VK_PIPELINE_BIND_POINT_GRAPHICS, GetShadersAsArray(desc), desc.pipelineLayout },
    scissorEnabled_    { desc.rasterizer.scissorTestEnabled                                                    },
    hasDynamicScissor_ { desc.scissors.empty()                                                                 }
{
    /* Get render pass from descriptor or default render pass */
    const RenderPass* renderPass = (desc.renderPass != nullptr ? desc.renderPass : defaultRenderPass);
    LLGL_ASSERT_PTR(renderPass);

    /* Create Vulkan graphics pipeline object */
    const VKRenderPass* renderPassVK = LLGL_CAST(const VKRenderPass*, renderPass);
    if (VKPipelineCache* pipelineCacheVK = (pipelineCache != nullptr ? LLGL_CAST(VKPipelineCache*, pipelineCache) : nullptr))
        CreateVkPipeline(device, *renderPassVK, limits, desc, pipelineCacheVK->GetNative());
    else
        CreateVkPipeline(device, *renderPassVK, limits, desc);
}


/*
 * ======= Private: =======
 */

static void CreateInputAssemblyState(
    const GraphicsPipelineDescriptor&       desc,
    VkPipelineInputAssemblyStateCreateInfo& createInfo)
{
    /* Always enable primitive restart index for strip topologies, to be compatible with D3D11 and Metal */
    createInfo.sType                    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.flags                    = 0;
    createInfo.topology                 = VKTypes::Map(desc.primitiveTopology);
    createInfo.primitiveRestartEnable   = VKBoolean(IsPrimitiveTopologyStrip(desc.primitiveTopology));
}

static void CreateTessellationState(
    const GraphicsPipelineDescriptor&       desc,
    VkPipelineTessellationStateCreateInfo&  createInfo)
{
    createInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    createInfo.pNext                = nullptr;
    createInfo.flags                = 0;
    createInfo.patchControlPoints   = GetPrimitiveTopologyPatchSize(desc.primitiveTopology);
}

static void CreateViewportState(
    const GraphicsPipelineDescriptor&   desc,
    VkPipelineViewportStateCreateInfo&  createInfo,
    std::vector<VkViewport>&            viewportsVK,
    std::vector<VkRect2D>&              scissorsVK)
{
    const std::size_t numViewports = desc.viewports.size();
    const std::size_t numScissors = desc.scissors.size();

    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.flags = 0;

    /* Initialize viewports */
    if (numViewports > 0)
    {
        createInfo.viewportCount = static_cast<std::uint32_t>(numViewports);

        /* Convert viewports to Vulkan structure */
        viewportsVK.resize(numViewports);

        for_range(i, numViewports)
            VKTypes::Convert(viewportsVK[i], desc.viewports[i]);

        createInfo.pViewports = viewportsVK.data();
    }
    else
    {
        /* Set viewport count to 1 (required), but set array to null pointer (will be ignored) */
        createInfo.viewportCount    = 1;
        createInfo.pViewports       = nullptr;
    }

    /* Convert scissors to Vulkan structure */
    if (numViewports > 0)
    {
        createInfo.scissorCount = static_cast<std::uint32_t>(numViewports);
        scissorsVK.resize(numViewports);

        for_range(i, numViewports)
        {
            if (i < numScissors)
                VKTypes::Convert(scissorsVK[i], desc.scissors[i]);
            else
                VKTypes::Convert(scissorsVK[i], desc.viewports[i]);
        }

        createInfo.pScissors = scissorsVK.data();
    }
    else
    {
        /* Set scissor count to 1 (required), but set array to null pointer (will be ignored) */
        createInfo.scissorCount = 1;
        createInfo.pScissors    = nullptr;
    }
}

static void CreateRasterizerState(
    const RasterizerDescriptor&                             desc,
    const VKGraphicsPipelineLimits&                         limits,
    VkPipelineRasterizationStateCreateInfo&                 createInfo,
    VkPipelineRasterizationConservativeStateCreateInfoEXT&  createInfoConservativeRasterExt)
{
    createInfo.sType                    = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.flags                    = 0;
    createInfo.depthClampEnable         = VKBoolean(desc.depthClampEnabled);
    createInfo.rasterizerDiscardEnable  = VKBoolean(desc.discardEnabled);
    createInfo.polygonMode              = VKTypes::Map(desc.polygonMode);
    createInfo.cullMode                 = VKTypes::Map(desc.cullMode);
    createInfo.frontFace                = (desc.frontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
    createInfo.depthBiasEnable          = VKBoolean(desc.depthBias.constantFactor != 0.0f || desc.depthBias.slopeFactor != 0.0f || desc.depthBias.clamp != 0.0f);
    createInfo.depthBiasConstantFactor  = desc.depthBias.constantFactor;
    createInfo.depthBiasClamp           = desc.depthBias.clamp;
    createInfo.depthBiasSlopeFactor     = desc.depthBias.slopeFactor;
    createInfo.lineWidth                = std::max(limits.lineWidthRange[0], std::min(desc.lineWidth, limits.lineWidthRange[1]));

    if (desc.conservativeRasterization)
    {
        LLGL_ASSERT_VK_EXT(EXT_conservative_rasterization);

        createInfo.pNext = &createInfoConservativeRasterExt;
        {
            createInfoConservativeRasterExt.sType                               = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
            createInfoConservativeRasterExt.pNext                               = nullptr;
            createInfoConservativeRasterExt.flags                               = 0;
            createInfoConservativeRasterExt.conservativeRasterizationMode       = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
            createInfoConservativeRasterExt.extraPrimitiveOverestimationSize    = 0.0f;
        }
    }
}

static void CreateMultisampleState(
    const VkSampleCountFlagBits             sampleCountBits,
    const BlendDescriptor&                  blendDesc,
    VkPipelineMultisampleStateCreateInfo&   createInfo)
{
    createInfo.sType                    = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.flags                    = 0;
    createInfo.rasterizationSamples     = sampleCountBits;
    createInfo.sampleShadingEnable      = VK_FALSE;
    createInfo.minSampleShading         = 0.0f;
    createInfo.pSampleMask              = static_cast<const VkSampleMask*>(&(blendDesc.sampleMask));
    createInfo.alphaToCoverageEnable    = VKBoolean(blendDesc.alphaToCoverageEnabled);
    createInfo.alphaToOneEnable         = VK_FALSE;
}

static void CreateStencilOpState(
    const StencilFaceDescriptor&    desc,
    VkStencilOpState&               createInfo)
{
    createInfo.failOp       = VKTypes::Map(desc.stencilFailOp);
    createInfo.passOp       = VKTypes::Map(desc.depthPassOp);
    createInfo.depthFailOp  = VKTypes::Map(desc.depthFailOp);
    createInfo.compareOp    = VKTypes::Map(desc.compareOp);
    createInfo.compareMask  = desc.readMask;
    createInfo.writeMask    = desc.writeMask;
    createInfo.reference    = desc.reference;
}

static void CreateDepthStencilState(
    const GraphicsPipelineDescriptor&       desc,
    VkPipelineDepthStencilStateCreateInfo&  createInfo)
{
    createInfo.sType                    = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.flags                    = 0;
    createInfo.depthTestEnable          = VKBoolean(desc.depth.testEnabled);
    createInfo.depthWriteEnable         = VKBoolean(desc.depth.writeEnabled);
    createInfo.depthCompareOp           = VKTypes::Map(desc.depth.compareOp);
    createInfo.depthBoundsTestEnable    = VK_FALSE;
    createInfo.stencilTestEnable        = VKBoolean(desc.stencil.testEnabled);
    CreateStencilOpState(desc.stencil.front, createInfo.front);
    CreateStencilOpState(desc.stencil.back, createInfo.back);
    createInfo.minDepthBounds           = 0.0f;
    createInfo.maxDepthBounds           = 1.0f;
}

static void CreateColorBlendAttachmentState(
    VkPipelineColorBlendAttachmentState&    createInfo,
    const BlendTargetDescriptor&            desc)
{
    createInfo.blendEnable          = VKBoolean(desc.blendEnabled);
    createInfo.srcColorBlendFactor  = VKTypes::Map(desc.srcColor);
    createInfo.dstColorBlendFactor  = VKTypes::Map(desc.dstColor);
    createInfo.colorBlendOp         = VKTypes::Map(desc.colorArithmetic);
    createInfo.srcAlphaBlendFactor  = VKTypes::Map(desc.srcAlpha);
    createInfo.dstAlphaBlendFactor  = VKTypes::Map(desc.dstAlpha);
    createInfo.alphaBlendOp         = VKTypes::Map(desc.alphaArithmetic);
    createInfo.colorWriteMask       = VKTypes::ToVkColorComponentFlags(desc.colorMask);
}

static void CreateColorBlendState(
    const BlendDescriptor&                              desc,
    VkPipelineColorBlendStateCreateInfo&                createInfo,
    std::vector<VkPipelineColorBlendAttachmentState>&   attachmentStatesVK,
    std::uint32_t                                       numColorAttachments)
{
    numColorAttachments = std::min(numColorAttachments, LLGL_MAX_NUM_COLOR_ATTACHMENTS);

    createInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    createInfo.pNext                = nullptr;
    createInfo.flags                = 0;

    if (desc.logicOp != LogicOp::Disabled)
    {
        createInfo.logicOpEnable    = VK_TRUE;
        createInfo.logicOp          = VKTypes::Map(desc.logicOp);
    }
    else
    {
        createInfo.logicOpEnable    = VK_FALSE;
        createInfo.logicOp          = VK_LOGIC_OP_NO_OP;
    }

    /* Convert blend targets to Vulkan structure */
    attachmentStatesVK.resize(numColorAttachments);
    for_range(i, numColorAttachments)
    {
        CreateColorBlendAttachmentState(
            attachmentStatesVK[i],
            desc.targets[desc.independentBlendEnabled ? i : 0]
        );
    }

    createInfo.attachmentCount      = numColorAttachments;
    createInfo.pAttachments         = attachmentStatesVK.data();
    createInfo.blendConstants[0]    = desc.blendFactor[0];
    createInfo.blendConstants[1]    = desc.blendFactor[1];
    createInfo.blendConstants[2]    = desc.blendFactor[2];
    createInfo.blendConstants[3]    = desc.blendFactor[3];
}

static void CreateDynamicState(
    const GraphicsPipelineDescriptor&   desc,
    VkPipelineDynamicStateCreateInfo&   createInfo,
    std::vector<VkDynamicState>&        dynamicStatesVK)
{
    if (desc.viewports.empty())
        dynamicStatesVK.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    if (desc.scissors.empty())
        dynamicStatesVK.push_back(VK_DYNAMIC_STATE_SCISSOR);
    if (desc.blend.blendFactorDynamic)
        dynamicStatesVK.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
    if (desc.stencil.referenceDynamic)
        dynamicStatesVK.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

    createInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    createInfo.pNext                = nullptr;
    createInfo.flags                = 0;
    createInfo.dynamicStateCount    = static_cast<std::uint32_t>(dynamicStatesVK.size());
    createInfo.pDynamicStates       = (dynamicStatesVK.empty() ? nullptr : dynamicStatesVK.data());
}

// Deep copy of the native create info of a graphics pipeline, so Y'CbCr variants can be created after the PSO descriptor is gone.
struct VKGraphicsPSO::CreateInfoStorage
{
    SmallVector<VkPipelineShaderStageCreateInfo, 5>         shaderStages;
    VKPtr<VkShaderModule>                                   shaderModules[5];   // Only owned for PSOs with Y'CbCr variants
    std::string                                             entryPoints[5];
    std::vector<VkVertexInputBindingDescription>            vertexBindings;
    std::vector<VkVertexInputAttributeDescription>          vertexAttribs;
    VkPipelineVertexInputStateCreateInfo                    vertexInputState;
    VkPipelineInputAssemblyStateCreateInfo                  inputAssemblyState;
    VkPipelineTessellationStateCreateInfo                   tessellationState;
    std::vector<VkViewport>                                 viewports;
    std::vector<VkRect2D>                                   scissors;
    VkPipelineViewportStateCreateInfo                       viewportState;
    VkPipelineRasterizationStateCreateInfo                  rasterizerState;
    VkPipelineRasterizationConservativeStateCreateInfoEXT   conservativeRasterState;
    VkSampleMask                                            sampleMask;
    VkPipelineMultisampleStateCreateInfo                    multisampleState;
    VkPipelineDepthStencilStateCreateInfo                   depthStencilState;
    std::vector<VkPipelineColorBlendAttachmentState>        blendAttachments;
    VkPipelineColorBlendStateCreateInfo                     colorBlendState;
    std::vector<VkDynamicState>                             dynamicStates;
    VkPipelineDynamicStateCreateInfo                        dynamicState;
    VkGraphicsPipelineCreateInfo                            createInfo;
};

VKGraphicsPSO::~VKGraphicsPSO()
{
    // dummy; required for std::unique_ptr of incomplete type
}

bool VKGraphicsPSO::FillCreateInfoStorage(
    CreateInfoStorage&                  storage,
    const VKRenderPass&                 renderPass,
    const VKGraphicsPipelineLimits&     limits,
    const GraphicsPipelineDescriptor&   desc)
{
    /* Get shader program object */
    const VKShader* vertexShaderVK = LLGL_CAST(const VKShader*, desc.vertexShader);
    if (vertexShaderVK == nullptr)
    {
        GetMutableReport().Errorf("cannot create Vulkan graphics pipeline without vertex shader\n");
        return false;
    }

    /* Pipeline variants are created after the shaders might have been released, so they need their own shader modules and entry point names */
    const bool ownsShaderModules = HasYcbcrVariants();

    auto FillAndAppendShaderStageCreateInfo = [this, &desc, &storage, ownsShaderModules](Shader* shader, bool& outShaderCreationFailed)
    {
        if (shader != nullptr)
        {
            VKShader& shaderVK = LLGL_CAST(VKShader&, *shader);
            const Report* report = shaderVK.GetReport();
            if (report != nullptr && report->HasErrors())
            {
                GetMutableReport().Errorf("Failed to load %s shader into Vulkan graphics pipeline state [%s]\n", ToString(shader->GetType()), GetOptionalDebugName(desc.debugName));
                outShaderCreationFailed = true;
            }
            else
            {
                const std::size_t shaderIndex = storage.shaderStages.size();
                storage.shaderStages.resize(shaderIndex + 1);
                VkPipelineShaderStageCreateInfo& stage = storage.shaderStages.back();
                this->GetShaderCreateInfoAndOptionalPermutation(shaderVK, stage, (ownsShaderModules ? &(storage.shaderModules[shaderIndex]) : nullptr));
                storage.entryPoints[shaderIndex] = stage.pName;
                stage.pName = storage.entryPoints[shaderIndex].c_str();
            }
        }
    };

    /* Get shader stages */
    bool shaderCreationFailed = false;
    FillAndAppendShaderStageCreateInfo(desc.vertexShader,           shaderCreationFailed);
    FillAndAppendShaderStageCreateInfo(desc.tessControlShader,      shaderCreationFailed);
    FillAndAppendShaderStageCreateInfo(desc.tessEvaluationShader,   shaderCreationFailed);
    FillAndAppendShaderStageCreateInfo(desc.geometryShader,         shaderCreationFailed);
    FillAndAppendShaderStageCreateInfo(desc.fragmentShader,         shaderCreationFailed);
    if (shaderCreationFailed)
        return false;

    /* Initialize vertex input descriptor and copy its arrays, since they are owned by the vertex shader */
    vertexShaderVK->FillVertexInputStateCreateInfo(storage.vertexInputState);
    {
        const VkPipelineVertexInputStateCreateInfo& vertexInput = storage.vertexInputState;
        storage.vertexBindings.assign(vertexInput.pVertexBindingDescriptions, vertexInput.pVertexBindingDescriptions + vertexInput.vertexBindingDescriptionCount);
        storage.vertexAttribs.assign(vertexInput.pVertexAttributeDescriptions, vertexInput.pVertexAttributeDescriptions + vertexInput.vertexAttributeDescriptionCount);
        storage.vertexInputState.pVertexBindingDescriptions     = (storage.vertexBindings.empty() ? nullptr : storage.vertexBindings.data());
        storage.vertexInputState.pVertexAttributeDescriptions   = (storage.vertexAttribs.empty() ? nullptr : storage.vertexAttribs.data());
    }

    /* Initialize input assembly state */
    CreateInputAssemblyState(desc, storage.inputAssemblyState);

    /* Initialize tessellation state */
    CreateTessellationState(desc, storage.tessellationState);

    /* Initialize viewport state */
    CreateViewportState(desc, storage.viewportState, storage.viewports, storage.scissors);

    /* Initialize rasterizer state */
    CreateRasterizerState(desc.rasterizer, limits, storage.rasterizerState, storage.conservativeRasterState);

    /* Initialize multi-sample state and copy sample mask, since it is owned by the descriptor */
    const VkSampleCountFlagBits sampleCountBits = (desc.rasterizer.multiSampleEnabled ? renderPass.GetSampleCountBits() : VK_SAMPLE_COUNT_1_BIT);
    CreateMultisampleState(sampleCountBits, desc.blend, storage.multisampleState);
    storage.sampleMask = static_cast<VkSampleMask>(desc.blend.sampleMask);
    storage.multisampleState.pSampleMask = &(storage.sampleMask);

    /* Initialize depth-stencil state */
    CreateDepthStencilState(desc, storage.depthStencilState);

    /* Initialize color-blend state */
    CreateColorBlendState(desc.blend, storage.colorBlendState, storage.blendAttachments, renderPass.GetNumColorAttachments());

    /* Initialize dynamic state */
    CreateDynamicState(desc, storage.dynamicState, storage.dynamicStates);

    /* Initialize graphics pipeline state object; the layout is specified by the caller */
    VkGraphicsPipelineCreateInfo& createInfo = storage.createInfo;
    {
        createInfo.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.pNext                = nullptr;
        createInfo.flags                = 0;
        createInfo.stageCount           = static_cast<std::uint32_t>(storage.shaderStages.size());
        createInfo.pStages              = storage.shaderStages.data();
        createInfo.pVertexInputState    = &(storage.vertexInputState);
        createInfo.pInputAssemblyState  = &(storage.inputAssemblyState);
        createInfo.pTessellationState   = (storage.inputAssemblyState.topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST ? &(storage.tessellationState) : nullptr);
        createInfo.pViewportState       = &(storage.viewportState);
        createInfo.pRasterizationState  = &(storage.rasterizerState);
        createInfo.pMultisampleState    = &(storage.multisampleState);
        createInfo.pDepthStencilState   = &(storage.depthStencilState);
        createInfo.pColorBlendState     = &(storage.colorBlendState);
        createInfo.pDynamicState        = (!storage.dynamicStates.empty() ? &(storage.dynamicState) : nullptr);
        createInfo.layout               = VK_NULL_HANDLE;
        createInfo.renderPass           = renderPass.GetVkRenderPass();
        createInfo.subpass              = 0;
        createInfo.basePipelineHandle   = VK_NULL_HANDLE;
        createInfo.basePipelineIndex    = 0;
    }

    return true;
}

bool VKGraphicsPSO::CreateVkPipeline(
    VkDevice                            device,
    const VKRenderPass&                 renderPass,
    const VKGraphicsPipelineLimits&     limits,
    const GraphicsPipelineDescriptor&   desc,
    VkPipelineCache                     pipelineCache)
{
    /* Storage must not be moved after it has been filled, since the create info refers to its members */
    auto storage = MakeUnique<CreateInfoStorage>();
    if (!FillCreateInfoStorage(*storage, renderPass, limits, desc))
        return false;

    if (HasYcbcrVariants())
    {
        /* Keep create info for pipeline variants that are created when a texture with Y'CbCr conversion is bound */
        renderPass_         = &renderPass;
        createInfoStorage_  = std::move(storage);
        return true;
    }

    /* Create graphics pipeline state object */
    storage->createInfo.layout = GetVkPipelineLayout();
    VkResult result = vkCreateGraphicsPipelines(device, pipelineCache, 1, &(storage->createInfo), nullptr, ReleaseAndGetAddressOfVkPipeline());
    VKThrowIfFailed(result, "failed to create Vulkan graphics pipeline");

    return true;
}

bool VKGraphicsPSO::CreateVkPipelineVariant(VkPipelineLayout pipelineLayout, VKPtr<VkPipeline>& outPipeline)
{
    if (!createInfoStorage_ || renderPass_ == nullptr)
        return false;

    /*
    Variants only differ in their layout (i.e. the immutable Y'CbCr sampler). The render pass is queried again,
    since the native render pass of a swap-chain is re-created when the swap-chain is resized.
    */
    VkGraphicsPipelineCreateInfo createInfo = createInfoStorage_->createInfo;
    {
        createInfo.layout       = pipelineLayout;
        createInfo.renderPass   = renderPass_->GetVkRenderPass();
    }
    VkResult result = vkCreateGraphicsPipelines(GetVkDevice(), VK_NULL_HANDLE, 1, &createInfo, nullptr, outPipeline.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan graphics pipeline variant for Y'CbCr conversion");

    return true;
}


} // /namespace LLGL



// ================================================================================
