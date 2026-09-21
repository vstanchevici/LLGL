/*
 * DbgPipelineLayout.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "DbgPipelineLayout.h"
#include "../DbgCore.h"


namespace LLGL
{


// Returns the index of the binding with the specified name, or ~0u if there is no such binding.
static std::uint32_t FindBindingIndexByName(const std::vector<BindingDescriptor>& bindings, const StringLiteral& name)
{
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        if (bindings[i].name.compare(name) == 0)
            return static_cast<std::uint32_t>(i);
    }
    return ~0u;
}

DbgPipelineLayout::DbgPipelineLayout(PipelineLayout& instance, const PipelineLayoutDescriptor& desc) :
    instance { instance             },
    desc     { desc                 },
    label    { LLGL_DBG_LABEL(desc) }
{
    /* Find combined texture-sampler whose sampler has a Y'CbCr conversion (see BindFlags::SamplerYcbcrConversion) */
    for (const CombinedTextureSamplerDescriptor& combinedDesc : desc.combinedTextureSamplers)
    {
        const std::uint32_t samplerIndex = FindBindingIndexByName(desc.bindings, combinedDesc.samplerName);
        if (samplerIndex != ~0u && (desc.bindings[samplerIndex].bindFlags & BindFlags::SamplerYcbcrConversion) != 0)
        {
            ycbcrTextureDescriptor = FindBindingIndexByName(desc.bindings, combinedDesc.textureName);
            ycbcrSamplerDescriptor = samplerIndex;
            break;
        }
    }
}

void DbgPipelineLayout::SetDebugName(const char* name)
{
    DbgSetObjectName(*this, name);
}

std::uint32_t DbgPipelineLayout::GetNumHeapBindings() const
{
    return instance.GetNumHeapBindings();
}

std::uint32_t DbgPipelineLayout::GetNumBindings() const
{
    return instance.GetNumBindings();
}

std::uint32_t DbgPipelineLayout::GetNumStaticSamplers() const
{
    return instance.GetNumStaticSamplers();
}

std::uint32_t DbgPipelineLayout::GetNumUniforms() const
{
    return instance.GetNumUniforms();
}


} // /namespace LLGL



// ================================================================================
