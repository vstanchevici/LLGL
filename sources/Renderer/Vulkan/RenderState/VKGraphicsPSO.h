/*
 * VKGraphicsPSO.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_GRAPHICS_PSO_H
#define LLGL_VK_GRAPHICS_PSO_H


#include "VKPipelineState.h"
#include <memory>


namespace LLGL
{


// Vulkan graphics pipeline limitations structure.
struct VKGraphicsPipelineLimits
{
    float lineWidthRange[2];
    float lineWidthGranularity;
};

struct GraphicsPipelineDescriptor;
class RenderPass;
class VKRenderPass;
class PipelineCache;

class VKGraphicsPSO final : public VKPipelineState
{

    public:

        VKGraphicsPSO(
            VkDevice                            device,
            const RenderPass*                   defaultRenderPass,
            const GraphicsPipelineDescriptor&   desc,
            const VKGraphicsPipelineLimits&     limits,
            PipelineCache*                      pipelineCache       = nullptr
        );

        ~VKGraphicsPSO();

        // Returns true if scissors are enabled.
        inline bool IsScissorEnabled() const
        {
            return scissorEnabled_;
        }

        // Returns true if this graphics pipeline has dynamic scissor state enabled (allows 'vkCmdSetScissor' commands).
        inline bool HasDynamicScissor() const
        {
            return hasDynamicScissor_;
        }

    protected:

        bool CreateVkPipelineVariant(VkPipelineLayout pipelineLayout, VKPtr<VkPipeline>& outPipeline) override;

    private:

        struct CreateInfoStorage;

    private:

        bool FillCreateInfoStorage(
            CreateInfoStorage&                  storage,
            const VKRenderPass&                 renderPass,
            const VKGraphicsPipelineLimits&     limits,
            const GraphicsPipelineDescriptor&   desc
        );

        bool CreateVkPipeline(
            VkDevice                            device,
            const VKRenderPass&                 renderPass,
            const VKGraphicsPipelineLimits&     limits,
            const GraphicsPipelineDescriptor&   desc,
            VkPipelineCache                     pipelineCache   = VK_NULL_HANDLE
        );

    private:

        bool                                scissorEnabled_     = false;
        bool                                hasDynamicScissor_  = false;

        /*
        Native create info and render pass for pipeline variants (see VKPipelineState::HasYcbcrVariants).
        The render pass must outlive this PSO; its native object is queried again for each variant.
        */
        std::unique_ptr<CreateInfoStorage>  createInfoStorage_;
        const VKRenderPass*                 renderPass_         = nullptr;

};


} // /namespace LLGL


#endif



// ================================================================================
