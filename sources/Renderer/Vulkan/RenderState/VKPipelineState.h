/*
 * VKPipelineState.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_VK_PIPELINE_STATE_H
#define LLGL_VK_PIPELINE_STATE_H


#include <LLGL/PipelineState.h>
#include <LLGL/Container/ArrayView.h>
#include "VKPipelineLayout.h"
#include "VKPipelineLayoutPermutation.h"
#include "../Texture/VKYcbcrConversionPool.h"
#include <vulkan/vulkan.h>
#include "../VKPtr.h"
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>


namespace LLGL
{


class Shader;
class PipelineLayout;
class VKShader;
class VKPipelineLayout;

// Native pipeline and layout permutation of a PSO for one Y'CbCr conversion (see BindFlags::SamplerYcbcrConversion).
struct VKYcbcrPipelineVariant
{
    VKYcbcrPipelineVariant(VkDevice device);
    ~VKYcbcrPipelineVariant();

    VKYcbcrConversionSPtr           conversion;         // Keeps the canonical sampler alive that is baked into the layout permutation
    VKPipelineLayoutPermutationSPtr layoutPermutation;
    VKPtr<VkPipeline>               pipeline;
};

class VKPipelineState : public PipelineState
{

    public:

        VKPipelineState(
            VkDevice                    device,
            VkPipelineBindPoint         bindPoint,
            const ArrayView<Shader*>&   shaders,
            const PipelineLayout*       pipelineLayout = nullptr
        );

        ~VKPipelineState();

        const Report* GetReport() const override;

    public:

        /*
        Binds the specified native pipeline and optional static descriptor sets (for immutable samplers) to the specified Vulkan command buffer.
        The pipeline and layout are either GetVkPipeline() and GetVkPipelineLayout() or those of a Y'CbCr variant (see GetOrCreateYcbcrVariant).
        */
        void BindPipelineAndStaticDescriptorSet(VkCommandBuffer commandBuffer, VkPipeline pipeline, VkPipelineLayout layout);

        // Binds the specified descriptor set to the dynamic descriptor set binding point.
        void BindDynamicDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkDescriptorSet descriptorSet);

        // Binds the specified descriptor set to teh heap descriptor set binding point.
        void BindHeapDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkDescriptorSet descriptorSet);

        // Pushes the specified values to the command buffer as push-constants.
        void PushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, std::uint32_t first, const char* data, std::uint32_t size);

        // Returns the native Vulkan pipeline layout this PSO was created with or the default layout if there was no layout specified.
        VkPipelineLayout GetVkPipelineLayout() const;

        /*
        Returns true if this PSO has a combined texture-sampler with Y'CbCr conversion (see BindFlags::SamplerYcbcrConversion).
        Such a PSO has no native pipeline by itself (i.e. GetVkPipeline() returns VK_NULL_HANDLE);
        instead, a variant is resolved when the texture is bound (see GetOrCreateYcbcrVariant).
        */
        inline bool HasYcbcrVariants() const
        {
            return hasYcbcrVariants_;
        }

        /*
        Returns the pipeline variant for the specified Y'CbCr conversion and creates it on the first request.
        Returns null if the native pipeline could not be created. This function is thread-safe.
        */
        const VKYcbcrPipelineVariant* GetOrCreateYcbcrVariant(const VKYcbcrConversionSPtr& conversion);

        // Returns the native PSO.
        inline VkPipeline GetVkPipeline() const
        {
            return pipeline_.Get();
        }

        // Returns the pipeline binding point.
        inline VkPipelineBindPoint GetBindPoint() const
        {
            return bindPoint_;
        }

        // Returns the pipeline layout this PSO was created with.
        inline const VKPipelineLayout* GetPipelineLayout() const
        {
            return pipelineLayout_;
        }

        // Returns the binding table and descriptor cache of this PSO's layout permutation.
        bool GetBindingTableAndDescriptorCache(const VKLayoutBindingTable*& outBindingTable, VKDescriptorCache*& outDescriptorCache) const;

    protected:

        // Releases the native PSO and returns its address.
        VkPipeline* ReleaseAndGetAddressOfVkPipeline();

        // Returns the Vulkan device this PSO was created with.
        inline VkDevice GetVkDevice() const
        {
            return device_;
        }

        /*
        Fills the native shader stage descriptor for the specified shader:
        - If the pipeline layout constaints uniforms, the shader module will be parsed for push constants.
        - If the shader module has a binding set mismatch with the pipeline layout,
          a permutation of the shader module will be created to match the internal binding set layout of the Vulkan backend.
        - If this PSO has Y'CbCr variants, the shader module is owned by the output module,
          since the pipeline variants are created after the shader might have been released.
        */
        void GetShaderCreateInfoAndOptionalPermutation(
            VKShader&                           shaderVK,
            VkPipelineShaderStageCreateInfo&    outCreateInfo,
            VKPtr<VkShaderModule>*              outOwnedShaderModule = nullptr
        );

        /*
        Creates the native pipeline of a Y'CbCr variant with the specified layout permutation.
        Only called for PSOs with Y'CbCr variants and must be implemented by subclasses that support them.
        */
        virtual bool CreateVkPipelineVariant(VkPipelineLayout pipelineLayout, VKPtr<VkPipeline>& outPipeline);

        // Returns the mutable report object.
        inline Report& GetMutableReport()
        {
            return report_;
        }

    private:

        void BindDescriptorSets(
            VkCommandBuffer         commandBuffer,
            VkPipelineLayout        layout,
            std::uint32_t           firstSet,
            std::uint32_t           descriptorSetCount,
            const VkDescriptorSet*  descriptorSets
        );

    private:

        VkDevice                                                device_             = VK_NULL_HANDLE;
        VKPtr<VkPipeline>                                       pipeline_;
        VKPipelineLayoutPermutationSPtr                         pipelineLayoutPerm_;
        const VKPipelineLayout*                                 pipelineLayout_     = nullptr;
        VkPipelineBindPoint                                     bindPoint_          = VK_PIPELINE_BIND_POINT_MAX_ENUM;
        std::vector<VkPushConstantRange>                        uniformRanges_;     // Push constant ranges; One range for each uniform descriptor. See UniformDescriptor.
        Report                                                  report_;

        bool                                                    hasYcbcrVariants_   = false;
        VKLayoutPermutationParameters                           ycbcrBaseParams_;   // Layout permutation parameters without immutable Y'CbCr sampler
        std::vector<std::unique_ptr<VKYcbcrPipelineVariant>>    ycbcrVariants_;
        std::mutex                                              ycbcrVariantsMutex_;

};


} // /namespace LLGL


#endif



// ================================================================================
