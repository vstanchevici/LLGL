/*
 * SamplerFlags.h
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#ifndef LLGL_SAMPLER_FLAGS_H
#define LLGL_SAMPLER_FLAGS_H


#include <LLGL/Export.h>
#include <LLGL/PipelineStateFlags.h>
#include <LLGL/TextureFlags.h>
#include <cstddef>
#include <cstdint>


namespace LLGL
{


/* ----- Enumerations ----- */

/**
\brief Technique for resolving texture coordinates that are outside of the range [0, 1].
\see SamplerDescriptor::addressModeU
\see SamplerDescriptor::addressModeV
\see SamplerDescriptor::addressModeW
*/
enum class SamplerAddressMode
{
    /**
    \brief Repeat texture coordinates within the interval [0, 1).
    \image html SamplerAddressMode_Repeat.png
    \image latex SamplerAddressMode_Repeat.png "SamplerAddressMode::Repeat example" width=0.3\textwidth
    */
    Repeat,

    /**
    \brief Flip texture coordinates at each integer junction.
    \image html SamplerAddressMode_Mirror.png
    \image latex SamplerAddressMode_Mirror.png "SamplerAddressMode::Mirror example" width=0.3\textwidth
    */
    Mirror,

    /**
    \brief Clamp texture coordinates to the interval [0, 1].
    \image html SamplerAddressMode_Clamp.png
    \image latex SamplerAddressMode_Clamp.png "SamplerAddressMode::Clamp example" width=0.3\textwidth
    */
    Clamp,

    /**
    \brief Sample border color for texture coordinates that are outside the interval [0, 1].
    \image html SamplerAddressMode_Border.png
    \image latex SamplerAddressMode_Border.png "SamplerAddressMode::Border example" width=0.3\textwidth
    \note Only supported on: desktop platforms (Windows, Linux, macOS).
    */
    Border,

    /**
    \brief Takes the absolute value of the texture coordinates and then clamps it to the interval [0, 1], i.e. mirror around 0.
    \image html SamplerAddressMode_MirrorOnce.png
    \image latex SamplerAddressMode_MirrorOnce.png "SamplerAddressMode::MirrorOnce example" width=0.3\textwidth
    \note Only supported on: desktop platforms (Windows, Linux, macOS).
    */
    MirrorOnce,
};

/**
\brief Sampling filter enumeration.
\see SamplerDescriptor::minFilter
\see SamplerDescriptor::magFilter
\see SamplerDescriptor::mipMapFilter
\see Image::Resize(const Extent3D&, const SamplerFilter)
*/
enum class SamplerFilter
{
    /**
    \brief Take the nearest texture sample.
    \image html SamplerFilter_Nearest.png
    \image latex SamplerFilter_Nearest.png "SamplerFilter::Nearest example" width=0.1\textwidth
    */
    Nearest,

    /**
    \brief Interpolate between multiple texture samples.
    \image html SamplerFilter_Linear.png
    \image latex SamplerFilter_Linear.png "SamplerFilter::Linear example" width=0.1\textwidth
    */
    Linear,
};

/**
\brief Color model conversion for Y'CbCr sampler conversions.
\see YcbcrConversionDescriptor::model
*/
enum class YcbcrModel
{
    RGBIdentity,    //!< Input is already RGB. The color values are only range-expanded if YcbcrRange::Narrow is specified.
    YcbcrIdentity,  //!< Range expansion only. The color values are not converted to RGB.
    Ycbcr709,       //!< Color model conversion from Y'CbCr to R'G'B' as defined in BT.709.
    Ycbcr601,       //!< Color model conversion from Y'CbCr to R'G'B' as defined in BT.601.
    Ycbcr2020,      //!< Color model conversion from Y'CbCr to R'G'B' as defined in BT.2020.
};

/**
\brief Numerical range of the encoded values for Y'CbCr sampler conversions.
\see YcbcrConversionDescriptor::range
*/
enum class YcbcrRange
{
    Full,   //!< The full range of the encoded values is valid, i.e. [0, 255] for 8-bit components.
    Narrow, //!< Headroom and foot room are reserved in the encoding, i.e. [16, 235] for luma and [16, 240] for chroma with 8-bit components.
};

/**
\brief Location of downsampled chroma samples relative to the luma samples.
\see YcbcrConversionDescriptor::xChromaOffset
\see YcbcrConversionDescriptor::yChromaOffset
*/
enum class ChromaLocation
{
    CositedEven,    //!< Chroma samples are aligned with the luma samples with even coordinates.
    Midpoint,       //!< Chroma samples are located half way between each even luma sample and the nearest higher odd luma sample.
};


/* ----- Structures ----- */

/**
\brief Y'CbCr sampler conversion descriptor structure.
\remarks A texture that is sampled with a Y'CbCr conversion must be created with the same conversion descriptor as the sampler.
Such a texture must be bound to a combined texture-sampler whose sampler binding has BindFlags::SamplerYcbcrConversion.
The conversion is performed by the hardware before the texel is returned to the shader, i.e. the shader receives RGB values.
\remarks A conversion is only applied if its format is multi-planar (see IsMultiPlanarFormat). Otherwise, the conversion is ignored.
\note Only supported with: Vulkan.
\see SamplerDescriptor::ycbcrConversion
\see TextureDescriptor::ycbcrConversion
\see RenderingFeatures::hasSamplerYcbcrConversion
*/
struct YcbcrConversionDescriptor
{
    /**
    \brief Multi-planar texture format, e.g. Format::NV12. By default Format::Undefined.
    \see IsMultiPlanarFormat
    */
    Format              format                      = Format::Undefined;

    //! Color model conversion. By default YcbcrModel::Ycbcr709.
    YcbcrModel          model                       = YcbcrModel::Ycbcr709;

    //! Numerical range of the encoded values. By default YcbcrRange::Narrow.
    YcbcrRange          range                       = YcbcrRange::Narrow;

    //! Horizontal location of the chroma samples. By default ChromaLocation::Midpoint.
    ChromaLocation      xChromaOffset               = ChromaLocation::Midpoint;

    //! Vertical location of the chroma samples. By default ChromaLocation::Midpoint.
    ChromaLocation      yChromaOffset               = ChromaLocation::Midpoint;

    /**
    \brief Filter for chroma reconstruction. By default SamplerFilter::Linear.
    \remarks If the format does not support linear chroma filtering, the backend falls back to SamplerFilter::Nearest.
    */
    SamplerFilter       chromaFilter                = SamplerFilter::Linear;

    /**
    \brief Component swizzle that is applied before the color model conversion. Each component is mapped to its identity by default.
    */
    TextureSwizzleRGBA  swizzle;

    //! Specifies whether chroma reconstruction is forced to be explicit. By default false.
    bool                forceExplicitReconstruction = false;
};

/**
\brief Texture sampler descriptor structure.
\see RenderSystem::CreateSampler
*/
struct LLGL_EXPORT SamplerDescriptor
{
    /**
    \brief Optional name for debugging purposes. By default null.
    \remarks The final name of the native hardware resource is implementation defined.
    \see RenderSystemChild::SetDebugName
    */
    const char*         debugName       = nullptr;

    //! Sampler address mode in U direction (also X axis). By default SamplerAddressMode::Repeat.
    SamplerAddressMode  addressModeU    = SamplerAddressMode::Repeat;

    //! Sampler address mode in V direction (also Y axis). By default SamplerAddressMode::Repeat.
    SamplerAddressMode  addressModeV    = SamplerAddressMode::Repeat;

    //! Sampler address mode in W direction (also Z axis). By default SamplerAddressMode::Repeat.
    SamplerAddressMode  addressModeW    = SamplerAddressMode::Repeat;

    //! Minification filter. By default SamplerFilter::Linear.
    SamplerFilter       minFilter       = SamplerFilter::Linear;

    //! Magnification filter. By default SamplerFilter::Linear.
    SamplerFilter       magFilter       = SamplerFilter::Linear;

    //! MIP-mapping filter. By default SamplerFilter::Linear.
    SamplerFilter       mipMapFilter    = SamplerFilter::Linear;

    /**
    \brief Specifies whether MIP-mapping is enabled or disabled. By default true.
    \remarks If MIP-mapping is disabled, \c mipMapFilter is ignored.
    \remarks The number of MIP-maps a texture has is specified by the TextureDescriptor::mipLevels attribute.
    \see TextureDescriptor::mipLevels
    */
    bool                mipMapEnabled   = true;

    /**
    \brief MIP-mapping level-of-detail (LOD) bias (or rather offset). By default 0.
    \remarks For Metal and OpenGLES, the LOD bias can only be specified within the shader code.
    \note Only supported with: OpenGL (Desktop only), Vulkan, Direct3D 11, Direct3D 12.
    */
    float               mipMapLODBias   = 0.0f;

    //! Lower end of the MIP-map range. By default 0.
    float               minLOD          = 0.0f;

    //! Upper end of the MIP-map range. Must be greater than or equal to \c minLOD. By default 1000.
    float               maxLOD          = 1000.0f;

    /**
    \brief Maximal anisotropy in the range [1, 16].
    \note Only supported with: OpenGL (Desktop only), Vulkan, Direct3D 11, Direct3D 12, Metal.
    */
    std::uint32_t       maxAnisotropy   = 1;

    //! Specifies whether the compare operation for depth textures is to be used or not. By default false.
    bool                compareEnabled  = false;

    //! Compare operation for depth textures. By default CompareOp::Less.
    CompareOp           compareOp       = CompareOp::Less;

    /**
    \brief Border color vector with four components: red, green, blue, and alpha. By default transparent-black (0, 0, 0, 0).
    \note For Vulkan and Metal as well as static samplers in general, only three predefined border colors are supported:
    - Transparent black: <code>{0,0,0,0}</code>
    - Opaque black: <code>{0,0,0,1}</code>
    - Opaque white: <code>{1,1,1,1}</code>
    */
    float               borderColor[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };

    /**
    \brief Optional Y'CbCr sampler conversion. By default null.
    \remarks If this is non-null, the sampler can only be bound to a sampler binding with BindFlags::SamplerYcbcrConversion
    and the backend overrides the following attributes as required by the conversion:
    all address modes are SamplerAddressMode::Clamp, MIP-mapping and anisotropy are disabled, compare operations are disabled,
    and the min/mag filters equal YcbcrConversionDescriptor::chromaFilter unless the format supports separate reconstruction filters.
    \remarks The pointer is only read during the call to RenderSystem::CreateSampler.
    \note Only supported with: Vulkan.
    \see BindFlags::SamplerYcbcrConversion
    \see RenderingFeatures::hasSamplerYcbcrConversion
    */
    const YcbcrConversionDescriptor* ycbcrConversion = nullptr;
};


/* ----- Functions ----- */

//! Returns true if the specified Y'CbCr conversion descriptors are equal.
LLGL_EXPORT bool operator == (const YcbcrConversionDescriptor& lhs, const YcbcrConversionDescriptor& rhs);

//! Returns true if the specified Y'CbCr conversion descriptors are unequal.
LLGL_EXPORT bool operator != (const YcbcrConversionDescriptor& lhs, const YcbcrConversionDescriptor& rhs);


} // /namespace LLGL


#endif



// ================================================================================
