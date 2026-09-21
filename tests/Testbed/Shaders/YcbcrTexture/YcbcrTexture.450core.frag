/*
 * YcbcrTexture.450core.frag
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#version 450 core

layout(location = 0) in vec2 vTexCoord;

// Combined texture-sampler with Y'CbCr conversion (see LLGL::BindFlags::SamplerYcbcrConversion): the hardware returns RGB values
layout(binding = 0) uniform sampler2D ycbcrMap;

// Uniform to validate that push constants can be set before the Y'CbCr pipeline variant is resolved
layout(push_constant) uniform Params
{
    vec4 colorScale;
};

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(texture(ycbcrMap, vTexCoord).rgb * colorScale.rgb, 1.0);
}
