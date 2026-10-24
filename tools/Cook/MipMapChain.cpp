#include "MipMapChain.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

/// MipImage

Result<MipImage>
MipImage::Create(
    const uint32_t w, const uint32_t h, const TextureFormat& format, std::vector<std::byte>&& bytes)
{
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format");
    MLG_CHECKV(format.CheckDimensions(w, h, 1));
    MLG_CHECK(bytes.size() == format.GetBlockAlignedSizeofData(w, h),
        "Byte data size does not match expected dimensions and format");

    return MipImage(w, h, format, std::move(bytes));
}

Result<MipImage>
MipImage::Create(
    const uint32_t w, const uint32_t h, const TextureFormat& format, StbDataPtr&& stbData)
{
    MLG_CHECK(!format.IsUnknown(), "Unknown texture format");
    MLG_CHECKV(format.CheckDimensions(w, h, 1));
    MLG_CHECK(stbData, "Byte data cannot be empty");

    return MipImage(w, h, format, std::move(stbData));
}

Result<MipImage>
MipImage::Resize(const uint32_t newWidth, const uint32_t newHeight) const
{
    const TextureFormat& format = GetFormat();

    MLG_CHECKV(format == TextureFormat::GetRGBA(), "Only RGBA format is supported for resizing");
    MLG_CHECKV(format.CheckDimensions(newWidth, newHeight, 1));
    MLG_CHECKV(newWidth != GetWidth() || newHeight != GetHeight(),
        "New dimensions must be different from the original dimensions");

    const size_t byteCount = static_cast<size_t>(newWidth) * newHeight * format.GetChannelCount();

    std::vector<std::byte> mipBytes(byteCount);

    const void* srcP = GetBytes().data();
    void* dstP = mipBytes.data();

    const unsigned char* result = stbir_resize_uint8_srgb(static_cast<const stbi_uc*>(srcP),
        static_cast<int>(GetWidth()),
        static_cast<int>(GetHeight()),
        static_cast<int>(GetWidth() * format.GetChannelCount()),
        static_cast<stbi_uc*>(dstP),
        static_cast<int>(newWidth),
        static_cast<int>(newHeight),
        static_cast<int>(newWidth * format.GetChannelCount()),
        STBIR_RGBA);

    MLG_CHECK(result, "Failed to resize image");

    return MipImage::Create(newWidth, newHeight, format, std::move(mipBytes));
}

/// MipMapChain

Result<MipMapChain>
MipMapChain::Create(MipImage&& baseImage)
{
    const TextureFormat& format = baseImage.GetFormat();
    const size_t blockDimension = format.GetBlockDimension();
    MLG_CHECK(baseImage.GetWidth() % blockDimension == 0 && baseImage.GetHeight() % blockDimension == 0,
        "Base image texture dimensions must be block aligned for format: {}", format.GetName());

    const uint32_t mipCount =
        TextureFormat::GetMaxMipCount(baseImage.GetWidth(), baseImage.GetHeight());

    BoundedVector<MipImage> mipmaps(mipCount);
    mipmaps.emplace_back(std::move(baseImage));

    for(uint32_t mip = 1; mip < mipCount; ++mip)
    {
        const MipImage& prevImage = mipmaps.back();
        const uint32_t mipWidth = std::max(prevImage.GetWidth() >> 1, 1u);
        const uint32_t mipHeight = std::max(prevImage.GetHeight() >> 1, 1u);

        auto resizeResult = prevImage.Resize(mipWidth, mipHeight);
        MLG_CHECK(resizeResult, "Failed to resize image");

        mipmaps.emplace_back(std::move(*resizeResult));
    }

    return Create(std::move(mipmaps));
}

Result<MipMapChain>
MipMapChain::Create(BoundedVector<MipImage>&& mipmaps)
{
    MLG_CHECK(!mipmaps.empty(), "Mipmaps cannot be empty");

    const MipImage& baseImage = mipmaps[0];
    const uint32_t w = baseImage.GetWidth();
    const uint32_t h = baseImage.GetHeight();
    const TextureFormat format = baseImage.GetFormat();

    MLG_CHECK(mipmaps.size() <= TextureFormat::GetMaxMipCount(w, h), "Too many mipmaps");
    const size_t blockDimension = format.GetBlockDimension();
    MLG_CHECK(w % blockDimension == 0 && h % blockDimension == 0,
        "Base image texture dimensions must be block aligned for format: {}",
        format.GetName());

    uint32_t mipW = w;
    uint32_t mipH = h;

    for(const MipImage& mip : mipmaps)
    {
        MLG_CHECKV(mip.GetFormat() == format, "All mipmaps must have the same format");
        MLG_CHECKV(mip.GetWidth() == mipW, "Unexpected mip width: {}", mip.GetWidth());
        MLG_CHECKV(mip.GetHeight() == mipH, "Unexpected mip height: {}", mip.GetHeight());

        const size_t expectedSize = format.GetBlockAlignedSizeofData(mipW, mipH);
        MLG_CHECKV(mip.GetBytes().size() == expectedSize,
            "Unexpected mip byte size: {} (expected: {})",
            mip.GetBytes().size(),
            expectedSize);

        mipW = std::max(1u, mipW >> 1);
        mipH = std::max(1u, mipH >> 1);
    }

    const uint32_t mipCount = static_cast<uint32_t>(mipmaps.size());

    return MipMapChain(w, h, mipCount, format, std::move(mipmaps));
}