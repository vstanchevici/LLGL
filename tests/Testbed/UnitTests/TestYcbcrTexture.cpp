/*
 * TestYcbcrTexture.cpp
 *
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "Testbed.h"
#include <LLGL/Utils/TypeNames.h>
#include <algorithm>
#include <cmath>
#include <vector>

#if LLGL_TESTBED_VULKAN_HEADERS
#   include <LLGL/Backend/Vulkan/NativeHandle.h>
#endif


// Y'CbCr color with 8-bit components.
struct YcbcrColor8
{
    std::uint8_t y, cb, cr;
};

// Converts the specified BT.709 Y'CbCr color into 8-bit RGB (see Vulkan specification, "Color Model Conversion" and "Range Expansion").
static void ConvertBT709ToRGB(const YcbcrColor8& src, YcbcrRange range, bool sRGB, int (&dst)[3])
{
    float y, cb, cr;
    if (range == YcbcrRange::Narrow)
    {
        y   = (static_cast<float>(src.y ) -  16.0f) / 219.0f;
        cb  = (static_cast<float>(src.cb) - 128.0f) / 224.0f;
        cr  = (static_cast<float>(src.cr) - 128.0f) / 224.0f;
    }
    else
    {
        y   = static_cast<float>(src.y) / 255.0f;
        cb  = (static_cast<float>(src.cb) - 128.0f) / 255.0f;
        cr  = (static_cast<float>(src.cr) - 128.0f) / 255.0f;
    }

    const float Kr = 0.2126f;
    const float Kb = 0.0722f;
    const float Kg = 1.0f - Kr - Kb;

    const float rgb[3] =
    {
        y + (2.0f - 2.0f*Kr)*cr,
        y - (2.0f*Kb*(1.0f - Kb)/Kg)*cb - (2.0f*Kr*(1.0f - Kr)/Kg)*cr,
        y + (2.0f - 2.0f*Kb)*cb,
    };

    for (int i = 0; i < 3; ++i)
    {
        /* Values are written to the framebuffer as they are, i.e. only encoded for sRGB framebuffers */
        float c = std::max(0.0f, std::min(1.0f, rgb[i]));
        if (sRGB)
            c = (c <= 0.0031308f ? c*12.92f : 1.055f*std::pow(c, 1.0f/2.4f) - 0.055f);
        dst[i] = static_cast<int>(std::round(c * 255.0f));
    }
}

/*
Creates NV12 textures with four quadrants of constant Y'CbCr values and samples them with a Y'CbCr sampler conversion
via the standard texture and sampler bindings, where the sampler binding has the BindFlags::SamplerYcbcrConversion flag.
The framebuffer is split into three columns:
 - A: Narrow-range texture that is initialized on creation.
 - B: Narrow-range texture that is written via RenderSystem::WriteTexture().
 - C: Full-range texture, i.e. a different conversion, which requires another pipeline variant within the same render pass.
 - D (Vulkan only): Placeholder texture and sampler that reference the native image, Y'CbCr conversion, and sampler of texture A
   via Resource::SetNativeHandle without ownership, i.e. as if they had been created by the application.
A uniform is set before any texture is bound, i.e. before the pipeline variant is known, and must still be applied to all draw calls.
The framebuffer is read back and the center of each quadrant is compared against the expected RGB color of the BT.709 conversion.
*/
DEF_TEST( YcbcrTexture )
{
    const RenderingCapabilities& caps = renderer->GetRenderingCaps();
    const bool isNV12Supported = (std::find(caps.textureFormats.begin(), caps.textureFormats.end(), Format::NV12) != caps.textureFormats.end());

    if (!caps.features.hasSamplerYcbcrConversion || !isNV12Supported)
    {
        if (opt.verbose)
            Log::Printf("Skip YcbcrTexture test: Y'CbCr sampler conversion or NV12 format not supported by renderer\n");
        return TestResult::Skipped;
    }

    if (shaders[VSYcbcrTexture] == nullptr || shaders[PSYcbcrTexture] == nullptr)
    {
        Log::Errorf("Missing shaders for backend\n");
        return TestResult::FailedErrors;
    }

    // Generate NV12 image with four quadrants: white, black, red, and blue
    const YcbcrColor8 quadrantColors[4] =
    {
        { 235, 128, 128 }, // White
        {  16, 128, 128 }, // Black
        {  63, 102, 240 }, // Red
        {  32, 240, 118 }, // Blue
    };

    static constexpr std::uint32_t texSize = 64;

    auto GetQuadrant = [](std::uint32_t x, std::uint32_t y) -> std::uint32_t
    {
        return (y < texSize/2 ? 0 : 2) + (x < texSize/2 ? 0 : 1);
    };

    std::vector<std::uint8_t> imageData(texSize*texSize + (texSize/2)*(texSize/2)*2);
    {
        /* Luma plane at full resolution */
        for (std::uint32_t y = 0; y < texSize; ++y)
        {
            for (std::uint32_t x = 0; x < texSize; ++x)
                imageData[y*texSize + x] = quadrantColors[GetQuadrant(x, y)].y;
        }

        /* Interleaved CbCr plane at half resolution */
        std::uint8_t* chromaPlane = imageData.data() + texSize*texSize;
        for (std::uint32_t y = 0; y < texSize/2; ++y)
        {
            for (std::uint32_t x = 0; x < texSize/2; ++x)
            {
                const YcbcrColor8& color = quadrantColors[GetQuadrant(x*2, y*2)];
                chromaPlane[(y*texSize/2 + x)*2 + 0] = color.cb;
                chromaPlane[(y*texSize/2 + x)*2 + 1] = color.cr;
            }
        }
    }

    const ImageView imageView{ ImageFormat::Compressed, DataType::UInt8, imageData.data(), imageData.size() };

    // Create Y'CbCr conversions for narrow and full range
    YcbcrConversionDescriptor ycbcrNarrowDesc;
    {
        ycbcrNarrowDesc.format          = Format::NV12;
        ycbcrNarrowDesc.model           = YcbcrModel::Ycbcr709;
        ycbcrNarrowDesc.range           = YcbcrRange::Narrow;
        ycbcrNarrowDesc.chromaFilter    = SamplerFilter::Linear;
    }
    YcbcrConversionDescriptor ycbcrFullDesc = ycbcrNarrowDesc;
    {
        ycbcrFullDesc.range             = YcbcrRange::Full;
    }

    // Create sampler with Y'CbCr conversion; this is optional for binding, but validates that sampler and texture have the same conversion
    SamplerDescriptor samplerDesc;
    {
        samplerDesc.addressModeU    = SamplerAddressMode::Clamp;
        samplerDesc.addressModeV    = SamplerAddressMode::Clamp;
        samplerDesc.addressModeW    = SamplerAddressMode::Clamp;
        samplerDesc.mipMapEnabled   = false;
        samplerDesc.ycbcrConversion = &ycbcrNarrowDesc;
    }
    Sampler* ycbcrNarrowSampler = renderer->CreateSampler(samplerDesc);

    // Create PSO layout with standard texture and sampler bindings that are combined into one texture-sampler
    PipelineLayoutDescriptor psoLayoutDesc;
    {
        psoLayoutDesc.debugName = "psoLayoutYcbcrTexture";
        psoLayoutDesc.bindings  =
        {
            BindingDescriptor{ "ycbcrMap",     ResourceType::Texture, BindFlags::Sampled,                StageFlags::FragmentStage, BindingSlot(0) },
            BindingDescriptor{ "ycbcrSampler", ResourceType::Sampler, BindFlags::SamplerYcbcrConversion, StageFlags::FragmentStage, BindingSlot(1) },
        };
        psoLayoutDesc.combinedTextureSamplers =
        {
            CombinedTextureSamplerDescriptor{ "ycbcrMap", "ycbcrMap", "ycbcrSampler", BindingSlot(0) },
        };
        psoLayoutDesc.uniforms =
        {
            UniformDescriptor{ "colorScale", UniformType::Float4 },
        };
    }
    PipelineLayout* psoLayout = renderer->CreatePipelineLayout(psoLayoutDesc);

    GraphicsPipelineDescriptor psoDesc;
    {
        psoDesc.pipelineLayout  = psoLayout;
        psoDesc.renderPass      = swapChain->GetRenderPass();
        psoDesc.vertexShader    = shaders[VSYcbcrTexture];
        psoDesc.fragmentShader  = shaders[PSYcbcrTexture];
    }
    CREATE_GRAPHICS_PSO(pso, psoDesc, "psoYcbcrTexture");

    // Create NV12 texture with initial data
    TextureDescriptor texDesc;
    {
        texDesc.type            = TextureType::Texture2D;
        texDesc.bindFlags       = BindFlags::Sampled;
        texDesc.cpuAccessFlags  = 0;
        texDesc.miscFlags       = 0;
        texDesc.format          = Format::NV12;
        texDesc.extent          = Extent3D{ texSize, texSize, 1 };
        texDesc.mipLevels       = 1;
        texDesc.ycbcrConversion = &ycbcrNarrowDesc;
    }
    CREATE_TEXTURE(texNV12_A, texDesc, "texNV12_A", &imageView);

    if (texNV12_A->GetFormat() != Format::NV12)
    {
        Log::Errorf("Expected texture format LLGL::NV12, but got LLGL::%s\n", ToString(texNV12_A->GetFormat()));
        return TestResult::FailedErrors;
    }

    // Create second NV12 texture without initial data and write image data separately
    texDesc.miscFlags = MiscFlags::NoInitialData;
    CREATE_TEXTURE(texNV12_B, texDesc, "texNV12_B", nullptr);
    renderer->WriteTexture(*texNV12_B, TextureRegion{ Offset3D{}, Extent3D{ texSize, texSize, 1 } }, imageView);

    // Create third NV12 texture with full-range conversion
    texDesc.miscFlags       = 0;
    texDesc.ycbcrConversion = &ycbcrFullDesc;
    CREATE_TEXTURE(texNV12_C, texDesc, "texNV12_C", &imageView);

    // Wrap the native objects of texture A in a placeholder texture and sampler without ownership (Vulkan only)
    Texture* texNative = nullptr;
    Sampler* samplerNative = nullptr;

    #if LLGL_TESTBED_VULKAN_HEADERS
    if (renderer->GetRendererID() == RendererID::Vulkan)
    {
        Vulkan::ResourceNativeHandle texHandle = {};
        if (!texNV12_A->GetNativeHandle(&texHandle, sizeof(texHandle)) || texHandle.image.ycbcrConversion == VK_NULL_HANDLE || texHandle.image.ycbcrSampler == VK_NULL_HANDLE)
        {
            Log::Errorf("Failed to get native Vulkan image with Y'CbCr conversion from NV12 texture\n");
            return TestResult::FailedErrors;
        }

        TextureDescriptor placeholderDesc;
        {
            placeholderDesc.type        = TextureType::Texture2D;
            placeholderDesc.bindFlags   = BindFlags::Sampled;
            placeholderDesc.format      = Format::RGBA8UNorm;
            placeholderDesc.extent      = Extent3D{ 1, 1, 1 };
            placeholderDesc.mipLevels   = 1;
            placeholderDesc.miscFlags   = MiscFlags::NoInitialData;
        }
        CREATE_TEXTURE(texPlaceholder, placeholderDesc, "texNativeNV12", nullptr);
        texNative = texPlaceholder;

        if (!texNative->SetNativeHandle(&texHandle, sizeof(texHandle), /*own:*/ false))
        {
            Log::Errorf("Failed to set native Vulkan image with Y'CbCr conversion\n");
            return TestResult::FailedErrors;
        }

        Vulkan::ResourceNativeHandle samplerHandle = {};
        samplerNative = renderer->CreateSampler(SamplerDescriptor{});
        if (!ycbcrNarrowSampler->GetNativeHandle(&samplerHandle, sizeof(samplerHandle)) ||
            !samplerNative->SetNativeHandle(&samplerHandle, sizeof(samplerHandle), /*own:*/ false))
        {
            Log::Errorf("Failed to set native Vulkan sampler with Y'CbCr conversion\n");
            return TestResult::FailedErrors;
        }
    }
    #endif // /LLGL_TESTBED_VULKAN_HEADERS

    // Render all textures side by side; the conversion of each texture selects the pipeline variant
    Texture* readbackTex = nullptr;

    const std::uint32_t numColumns = (texNative != nullptr ? 4 : 3);
    const Extent2D resolution = swapChain->GetResolution();
    const Extent2D columnResolution{ resolution.width / numColumns, resolution.height };

    struct Column
    {
        Texture*    texture;
        Sampler*    sampler;
        YcbcrRange  range;
        const char* name;
    };

    const Column columns[4] =
    {
        { texNV12_A, ycbcrNarrowSampler, YcbcrRange::Narrow, "A" },
        { texNV12_B, nullptr,            YcbcrRange::Narrow, "B" },
        { texNV12_C, nullptr,            YcbcrRange::Full,   "C" },
        { texNative, samplerNative,      YcbcrRange::Narrow, "D" },
    };

    BEGIN();
    {
        cmdBuffer->SetVertexBuffer(*meshBuffer); // Dummy vertex buffer

        cmdBuffer->BeginRenderPass(*swapChain);
        {
            cmdBuffer->Clear(ClearFlags::Color, ClearValue{ 0.0f, 1.0f, 0.0f, 1.0f });
            cmdBuffer->SetPipelineState(*pso);

            const float colorScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            cmdBuffer->SetUniforms(0, colorScale, sizeof(colorScale));

            for (std::uint32_t i = 0; i < numColumns; ++i)
            {
                cmdBuffer->SetViewport(Viewport{ Offset2D{ static_cast<std::int32_t>(i * columnResolution.width), 0 }, columnResolution });
                cmdBuffer->SetResource(0, *columns[i].texture);
                if (columns[i].sampler != nullptr)
                    cmdBuffer->SetResource(1, *columns[i].sampler);
                cmdBuffer->Draw(3, 0);
            }

            readbackTex = CaptureFramebuffer(*cmdBuffer, swapChain->GetColorFormat(), resolution);
        }
        cmdBuffer->EndRenderPass();
    }
    END();

    // Read back framebuffer as RGBA8
    std::vector<std::uint8_t> pixels(resolution.width * resolution.height * 4);
    {
        MutableImageView dstImageView;
        {
            dstImageView.format     = ImageFormat::RGBA;
            dstImageView.dataType   = DataType::UInt8;
            dstImageView.data       = pixels.data();
            dstImageView.dataSize   = pixels.size();
        }
        renderer->ReadTexture(*readbackTex, TextureRegion{ Offset3D{}, Extent3D{ resolution.width, resolution.height, 1 } }, dstImageView);
    }

    // Save capture for inspection; this also releases the readback texture
    SaveCapture(readbackTex, "YcbcrTexture");

    // Compare center of each quadrant for all textures
    const bool isSRGB = ((GetFormatAttribs(swapChain->GetColorFormat()).flags & FormatFlags::IsColorSpace_sRGB) != 0);
    constexpr int threshold = 6;

    TestResult result = TestResult::Passed;

    for (std::uint32_t column = 0; column < numColumns; ++column)
    {
        for (std::uint32_t quadrant = 0; quadrant < 4; ++quadrant)
        {
            const std::uint32_t x = column*columnResolution.width + (quadrant % 2 == 0 ? 1 : 3) * columnResolution.width / 4;
            const std::uint32_t y = (quadrant / 2 == 0 ? 1 : 3) * resolution.height / 4;

            const std::uint8_t* actual = &pixels[(y*resolution.width + x)*4];

            int expected[3];
            ConvertBT709ToRGB(quadrantColors[quadrant], columns[column].range, isSRGB, expected);

            for (int c = 0; c < 3; ++c)
            {
                if (std::abs(static_cast<int>(actual[c]) - expected[c]) > threshold)
                {
                    Log::Errorf(
                        "Mismatch in Y'CbCr texture %s, quadrant %u, at pixel (%u, %u): expected RGB (%d, %d, %d), but got (%d, %d, %d)\n",
                        columns[column].name, quadrant, x, y,
                        expected[0], expected[1], expected[2],
                        static_cast<int>(actual[0]), static_cast<int>(actual[1]), static_cast<int>(actual[2])
                    );
                    result = TestResult::FailedMismatch;
                    break;
                }
            }
        }
    }

    // Clear resources; the native wrappers must be released first, since they only reference the native objects of texture A
    if (texNative != nullptr)
        renderer->Release(*texNative);
    if (samplerNative != nullptr)
        renderer->Release(*samplerNative);
    renderer->Release(*texNV12_A);
    renderer->Release(*texNV12_B);
    renderer->Release(*texNV12_C);
    renderer->Release(*pso);
    renderer->Release(*psoLayout);
    renderer->Release(*ycbcrNarrowSampler);

    return result;
}

