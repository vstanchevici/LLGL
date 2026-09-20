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


// Y'CbCr color with 8-bit components.
struct YcbcrColor8
{
    std::uint8_t y, cb, cr;
};

// Converts the specified narrow-range BT.709 Y'CbCr color into 8-bit RGB (see Vulkan specification, "Color Model Conversion").
static void ConvertNarrowBT709ToRGB(const YcbcrColor8& src, bool sRGB, int (&dst)[3])
{
    const float y   = (static_cast<float>(src.y ) -  16.0f) / 219.0f;
    const float cb  = (static_cast<float>(src.cb) - 128.0f) / 224.0f;
    const float cr  = (static_cast<float>(src.cr) - 128.0f) / 224.0f;

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
via an immutable combined texture-sampler (see BindingDescriptor::immutableSampler).
The left texture is initialized on creation and the right texture is written via RenderSystem::WriteTexture().
The framebuffer is read back and the center of each quadrant is compared against the expected RGB color of the narrow-range BT.709 conversion.
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

    // Create sampler with Y'CbCr conversion
    YcbcrConversionDescriptor ycbcrDesc;
    {
        ycbcrDesc.format        = Format::NV12;
        ycbcrDesc.model         = YcbcrModel::Ycbcr709;
        ycbcrDesc.range         = YcbcrRange::Narrow;
        ycbcrDesc.chromaFilter  = SamplerFilter::Linear;
    }
    SamplerDescriptor samplerDesc;
    {
        samplerDesc.addressModeU    = SamplerAddressMode::Clamp;
        samplerDesc.addressModeV    = SamplerAddressMode::Clamp;
        samplerDesc.addressModeW    = SamplerAddressMode::Clamp;
        samplerDesc.mipMapEnabled   = false;
        samplerDesc.ycbcrConversion = &ycbcrDesc;
    }
    Sampler* ycbcrSampler = renderer->CreateSampler(samplerDesc);

    // Create PSO layout with immutable sampler for combined texture-sampler
    BindingDescriptor texBinding{ "ycbcrMap", ResourceType::Texture, BindFlags::Sampled, StageFlags::FragmentStage, 0u };
    texBinding.immutableSampler = ycbcrSampler;

    PipelineLayoutDescriptor psoLayoutDesc;
    {
        psoLayoutDesc.debugName = "psoLayoutYcbcrTexture";
        psoLayoutDesc.bindings  = { texBinding };
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
        texDesc.ycbcrConversion = &ycbcrDesc;
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

    // Render both textures side by side
    Texture* readbackTex = nullptr;

    const Extent2D resolution = swapChain->GetResolution();
    const Extent2D halfResolution{ resolution.width / 2, resolution.height };

    BEGIN();
    {
        cmdBuffer->SetVertexBuffer(*meshBuffer); // Dummy vertex buffer

        cmdBuffer->BeginRenderPass(*swapChain);
        {
            cmdBuffer->Clear(ClearFlags::Color, ClearValue{ 0.0f, 1.0f, 0.0f, 1.0f });
            cmdBuffer->SetPipelineState(*pso);

            cmdBuffer->SetViewport(Viewport{ Offset2D{ 0, 0 }, halfResolution });
            cmdBuffer->SetResource(0, *texNV12_A);
            cmdBuffer->Draw(3, 0);

            cmdBuffer->SetViewport(Viewport{ Offset2D{ static_cast<std::int32_t>(halfResolution.width), 0 }, halfResolution });
            cmdBuffer->SetResource(0, *texNV12_B);
            cmdBuffer->Draw(3, 0);

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

    // Compare center of each quadrant for both textures
    const bool isSRGB = ((GetFormatAttribs(swapChain->GetColorFormat()).flags & FormatFlags::IsColorSpace_sRGB) != 0);
    constexpr int threshold = 6;

    TestResult result = TestResult::Passed;

    for (std::uint32_t half = 0; half < 2; ++half)
    {
        for (std::uint32_t quadrant = 0; quadrant < 4; ++quadrant)
        {
            const std::uint32_t x = half*halfResolution.width + (quadrant % 2 == 0 ? 1 : 3) * halfResolution.width / 4;
            const std::uint32_t y = (quadrant / 2 == 0 ? 1 : 3) * resolution.height / 4;

            const std::uint8_t* actual = &pixels[(y*resolution.width + x)*4];

            int expected[3];
            ConvertNarrowBT709ToRGB(quadrantColors[quadrant], isSRGB, expected);

            for (int c = 0; c < 3; ++c)
            {
                if (std::abs(static_cast<int>(actual[c]) - expected[c]) > threshold)
                {
                    Log::Errorf(
                        "Mismatch in Y'CbCr texture %s, quadrant %u, at pixel (%u, %u): expected RGB (%d, %d, %d), but got (%d, %d, %d)\n",
                        (half == 0 ? "A" : "B"), quadrant, x, y,
                        expected[0], expected[1], expected[2],
                        static_cast<int>(actual[0]), static_cast<int>(actual[1]), static_cast<int>(actual[2])
                    );
                    result = TestResult::FailedMismatch;
                    break;
                }
            }
        }
    }

    // Clear resources
    renderer->Release(*texNV12_A);
    renderer->Release(*texNV12_B);
    renderer->Release(*pso);
    renderer->Release(*psoLayout);
    renderer->Release(*ycbcrSampler);

    return result;
}
