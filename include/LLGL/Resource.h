/*
 * Resource.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_RESOURCE_H
#define LLGL_RESOURCE_H


#include <LLGL/RenderSystemChild.h>
#include <LLGL/ResourceFlags.h>
#include <cstddef>


namespace LLGL
{


/**
\brief Base class for all hardware resource interfaces.
\see Buffer
\see Texture
\see Sampler
*/
class LLGL_EXPORT Resource : public RenderSystemChild
{

        LLGL_DECLARE_INTERFACE( InterfaceID::Resource );

    public:

        /**
        \brief Returns the type of this resource object.
        \remarks This is queried by a virtual function call, so the resource type does not need to be stored per instance.
        \see ResourceType
        \todo (Maybe) replace by IsInstanceOf
        */
        virtual ResourceType GetResourceType() const = 0;

        /* ----- Extensions ----- */

        /**
        \brief Returns the native resource handle.

        \param[out] nativeHandle Raw pointer to the backend specific structure to store the native handle.
        Optain the respective structure from <code>#include <LLGL/Backend/BACKEND/NativeHandle.h></code>
        where \c BACKEND must be either \c Direct3D12, \c Direct3D11, \c Vulkan, \c Metal, or \c OpenGL.
        OpenGL does not have a native handle as it uses the current platform specific GL context.

        \param[in] nativeHandleSize Specifies the size (in bytes) of the native handle structure for robustness.
        This must be <code>sizeof(STRUCT)</code> where \c STRUCT is the respective backend specific structure such as \c LLGL::Direct3D12::ResourceNativeHandle.

        \return True if the native handle was successfully retrieved. Otherwise, \c nativeHandleSize specifies an incompatible structure size.

        \remarks For the Direct3D backends, all retrieved COM pointers will be incremented and the user is responsible for releasing those pointers,
        i.e. a call to \c IUnknown::Release is required to each of the objects returned by this function.
        \remarks For the Metal backend, all retrieved \c NSObject instances will have their retain counter incremented and the user is responsible for releasing those objects,
        i.e. a call to <code>-(oneway void)release</code> is required to each of the objects returned by this function.
        \remarks For backends that do not support this function, the return value is false unless \c nativeHandle is null or \c nativeHandleSize is 0.
        \remarks Example for obtaining the native handle of a Direct3D12 render system:
        \code
        #include <LLGL/Backend/Direct3D12/NativeHandle.h>
        //...
        LLGL::Direct3D12::ResourceNativeHandle d3dNativeHandle;
        if (myResource->GetNativeHandle(&d3dNativeHandle, sizeof(d3dNativeHandle))) {
            ID3D12Resource* d3dResource = d3dNativeHandle.resource;
            ...
            d3dResource->Release();
        }
        \endcode

        \see Direct3D12::ResourceNativeHandle
        \see Direct3D11::ResourceNativeHandle
        \see Vulkan::ResourceNativeHandle
        \see Metal::ResourceNativeHandle
        \see OpenGL::ResourceNativeHandle
        */
        virtual bool GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize) = 0;

        /**
        \brief Replaces the native object of this resource with the specified native handle, e.g. an image that was created by the application.

        \param[in] nativeHandle Raw pointer to the backend specific structure that describes the native object (same structures as for GetNativeHandle).
        \param[in] nativeHandleSize Specifies the size (in bytes) of the native handle structure.
        \param[in] own Specifies whether LLGL takes ownership of the native objects. By default false.
        If this is false, the native objects are only referenced and the caller must keep them alive as long as this resource
        (and any pipeline state it has been used with) is in use, and destroy them afterwards.
        If this is true, the native objects are destroyed together with this resource or when another native handle is set.

        \return True if the native handle was set. Otherwise, the backend does not support this for the resource or the native handle is invalid.

        \remarks The native object that LLGL previously created or referenced for this resource is released (if LLGL owned it).
        \remarks Supported with:
        - Vulkan: Textures (Vulkan::ResourceNativeHandle::image, optionally with a Y'CbCr conversion) and samplers (Vulkan::ResourceNativeHandle::sampler).
        - OpenGL: Textures (OpenGL::ResourceNativeHandle::texture, optionally with an explicit texture target such as \c GL_TEXTURE_EXTERNAL_OES) and native samplers.
        \remarks The caller is responsible for the synchronization and queue family ownership of the native objects, e.g. when they are shared with a video decoder.
        \see GetNativeHandle
        \see BindFlags::SamplerYcbcrConversion
        */
        virtual bool SetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize, bool own = false);
};


} // /namespace LLGL


#endif



// ================================================================================
