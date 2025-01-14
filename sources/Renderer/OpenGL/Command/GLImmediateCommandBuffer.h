/*
 * GLImmediateCommandBuffer.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_GL_IMMEDIATE_COMMAND_BUFFER_H
#define LLGL_GL_IMMEDIATE_COMMAND_BUFFER_H


#include "GLCommandBuffer.h"
#include <memory>


namespace LLGL
{


class GLRenderTarget;
class GLSwapChain;
class GLStateManager;
class GLRenderPass;

class GLImmediateCommandBuffer final : public GLCommandBuffer
{

    public:

        #include "GLCommandBuffer.inl"

    public:

        GLImmediateCommandBuffer();

    public:

        // Returns true.
        bool IsImmediateCmdBuffer() const override;

    private:

        void BindResource(GLResourceType type, GLuint slot, std::uint32_t descriptor, Resource& resource);
        void BindCombinedResource(GLResourceType type, const GLuint* slots, std::uint32_t numSlots, Resource& resource);

    private:

        GLStateManager* stateMngr_ = nullptr;

};


} // /namespace LLGL


#endif



// ================================================================================
