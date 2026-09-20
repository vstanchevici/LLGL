/*
 * YcbcrTexture.450core.frag
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#version 450 core

layout(location = 0) in vec2 vTexCoord;

// Combined texture-sampler with immutable Y'CbCr sampler: the hardware returns RGB values
layout(binding = 0) uniform sampler2D ycbcrMap;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(texture(ycbcrMap, vTexCoord).rgb, 1.0);
}
