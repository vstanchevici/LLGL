/*
 * GLVertexArrayObject.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_GL_VERTEX_ARRAY_OBJECT_H
#define LLGL_GL_VERTEX_ARRAY_OBJECT_H


#include "../OpenGL.h"
#include "GLVertexInputLayout.h"
#include <LLGL/Container/ArrayView.h>


namespace LLGL
{


class GLStateManager;
struct GLVertexAttribute;

// Wrapper class for an OpenGL Vertex-Array-Object (VAO), for GL 3.0+.
class GLVertexArrayObject
{

    public:

        GLVertexArrayObject() = default;

        // Releases the underlying GL VAO name; only safe to run while its owning GL context is current.
        ~GLVertexArrayObject();

        // Non-copyable/non-movable: owns a unique GL VAO name. Instances are only ever
        // constructed once (via std::make_shared in GL3PlusSharedContextVertexArray)
        // so relocation is never required.
        GLVertexArrayObject(const GLVertexArrayObject&) = delete;
        GLVertexArrayObject& operator=(const GLVertexArrayObject&) = delete;
        GLVertexArrayObject(GLVertexArrayObject&&) = delete;
        GLVertexArrayObject& operator=(GLVertexArrayObject&&) = delete;

        // Release VAO from GL context.
        void Release();

        // Builds the specified attribute using a 'glVertexAttrib*Pointer' function.
        void BuildVertexLayout(const GLVertexInputLayout& inputLayout);

        // Returns the ID of the hardware vertex-array-object (VAO)
        inline GLuint GetID() const
        {
            return id_;
        }

        // Returns the input layout hash from the last time BuildVertexLayout was called.
        inline std::size_t GetInputLayoutHash() const
        {
            return inputLayoutHash_;
        }

    private:

        void BuildVertexAttribute(const GLVertexAttribute& attribute);

    private:

        GLuint      id_                 = 0; // Vertex array object ID.
        GLuint      attribIndexEnd_     = 0; // Last VAO attribute index; This is needed when the input layout changes.
        std::size_t inputLayoutHash_    = 0;

};


} // /namespace LLGL


#endif



// ================================================================================
