#include "Compressors.h"

#include <bc7enc.h>

namespace
{
// Encodes an image as BC7.
Result<MipImage>
EncodeBC7Mip(const MipImage& srcMip, const std::string_view texturePath)
{
    MLG_CHECK(srcMip.GetFormat() == TextureFormat::GetRGBA(), "Source mip must be in RGBA format");

    constexpr uint32_t kChannelCount = 4;
    constexpr uint32_t kBlockPixelDimension = 4;

    const TextureFormat& bc7Format = TextureFormat::GetBC7();

    MLG_INFO("Encoding {} -> BC7...", texturePath);

    const uint32_t srcWidth = srcMip.GetWidth();
    const uint32_t srcHeight = srcMip.GetHeight();

    const size_t outputByteCount = bc7Format.GetBlockAlignedSizeofData(srcWidth, srcHeight);
    const size_t dstRowStride = bc7Format.GetBlockAlignedBytesPerRow(srcWidth);

    std::vector<std::byte> outputBytes(outputByteCount);

    // Compress.

    // Call once before using the encoder.
    static std::once_flag bc7enc_init_flag;
    std::call_once(bc7enc_init_flag, bc7enc_compress_block_init);

    bc7enc_compress_block_params params{};
    bc7enc_compress_block_params_init(&params);

    const size_t srcRowStride = static_cast<size_t>(srcWidth) * kChannelCount;
    const std::span<const std::byte> srcBytes = srcMip.GetBytes();
    std::span dstBytes = outputBytes;

    std::byte blockPixels[kBlockPixelDimension][kBlockPixelDimension * kChannelCount];

    const size_t blocksPerRow = bc7Format.GetBlocksPerRow(srcMip.GetWidth());
    const size_t blockRowCount = bc7Format.GetBlockRowCount(srcMip.GetHeight());

    for(uint32_t blockY = 0; blockY < blockRowCount; ++blockY)
    {
        const uint32_t srcY = blockY * kBlockPixelDimension;
        size_t dstRowOffset = 0;

        for(uint32_t blockX = 0; blockX < blocksPerRow;
            ++blockX, dstRowOffset += bc7Format.GetBytesPerBlock())
        {
            const uint32_t srcX = blockX * kBlockPixelDimension;

            const bool isFullBlock =
                srcWidth - srcX >= kBlockPixelDimension && srcHeight - srcY >= kBlockPixelDimension;

            if(isFullBlock)
            {
                size_t srcOffset = (static_cast<size_t>(srcY) * srcRowStride)
                    + (static_cast<size_t>(srcX) * kChannelCount);

                std::copy_n(srcBytes.subspan(srcOffset).data(),
                    kBlockPixelDimension * kChannelCount,
                    &blockPixels[0][0]);

                srcOffset += srcRowStride;
                std::copy_n(srcBytes.subspan(srcOffset).data(),
                    kBlockPixelDimension * kChannelCount,
                    &blockPixels[1][0]);

                srcOffset += srcRowStride;
                std::copy_n(srcBytes.subspan(srcOffset).data(),
                    kBlockPixelDimension * kChannelCount,
                    &blockPixels[2][0]);

                srcOffset += srcRowStride;
                std::copy_n(srcBytes.subspan(srcOffset).data(),
                    kBlockPixelDimension * kChannelCount,
                    &blockPixels[3][0]);
            }
            else
            {
                for(uint32_t y = 0; y < kBlockPixelDimension; ++y)
                {
                    const uint32_t sourceY = std::min(srcY + y, srcHeight - 1);

                    size_t dstOffset = 0;

                    for(uint32_t x = 0; x < kBlockPixelDimension; ++x, dstOffset += kChannelCount)
                    {
                        const uint32_t sourceX = std::min(srcX + x, srcWidth - 1);
                        const size_t srcOffset = (static_cast<size_t>(sourceY) * srcRowStride)
                            + (static_cast<size_t>(sourceX) * kChannelCount);

                        std::copy_n(srcBytes.subspan(srcOffset).data(),
                            kChannelCount,
                            &blockPixels[y][dstOffset]);
                    }
                }
            }

            // The return value indicates transparency, not success or failure.
            bc7enc_compress_block(&dstBytes[dstRowOffset], &blockPixels[0], &params);
        }

        dstBytes = dstBytes.subspan(dstRowStride);
    }

    return MipImage::Create(srcWidth, srcHeight, bc7Format, std::move(outputBytes));
}

} // namespace

Result<MipMapChain>
EncodeBC7(MipImage&& baseMip, const std::string_view texturePath)
{
    MLG_CHECK(baseMip.GetFormat() == TextureFormat::GetRGBA(), "Only RGBA format is supported for BC7 encoding");
    
    const TextureFormat& bc7Format = TextureFormat::GetBC7();

    // The base mip image must be block aligned for BC7 encoding
    
    const size_t blockAlignedWidth = bc7Format.GetBlockAlignedWidth(baseMip.GetWidth());
    const size_t blockAlignedHeight = bc7Format.GetBlockAlignedHeight(baseMip.GetHeight());

    if(baseMip.GetWidth() != blockAlignedWidth || baseMip.GetHeight() != blockAlignedHeight)
    {
        auto resizeResult = baseMip.Resize(static_cast<uint32_t>(blockAlignedWidth),
            static_cast<uint32_t>(blockAlignedHeight));
        MLG_CHECK(resizeResult, "Failed to resize base image to block aligned dimensions.");
        baseMip = std::move(*resizeResult);
    }

    auto mipmapsResult = MipMapChain::Create(std::move(baseMip));
    MLG_CHECK(mipmapsResult, "Failed to generate mipmaps for texture: {}", texturePath);

    BoundedVector<MipImage> mipmaps(mipmapsResult->GetMipCount());

    for(const MipImage& mip : mipmapsResult->GetMipMaps())
    {
        auto bc7Result = EncodeBC7Mip(mip, texturePath);
        MLG_CHECK(bc7Result, "Failed to encode BC7 mip level for texture: {}", texturePath);
        mipmaps.emplace_back(std::move(*bc7Result));
    }

    return MipMapChain::Create(std::move(mipmaps));
}