#include "Encoders.h"

#include <astcenc.h>
#include <bc7enc.h>
#include <memory>

namespace
{
// Encodes an image as BC7.
Result<MipImage>
EncodeBC7Mip(const MipImage& srcMip, const std::string_view texturePath, const uint32_t mipLevel)
{
    MLG_CHECK(srcMip.GetFormat() == TextureFormat::GetRGBA(), "Source mip must be in RGBA format");

    constexpr uint32_t kChannelCount = 4;
    constexpr uint32_t kBlockPixelDimension = 4;

    const TextureFormat& bc7Format = TextureFormat::GetBC7();

    MLG_INFO("Encoding {} -> BC7 (mip {})...", texturePath, mipLevel);

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

/// Deleter for astcenc_context to be used with smart pointers.
struct AstcContextDeleter
{
    void operator()(astcenc_context* context) const noexcept { astcenc_context_free(context); }
};

Result<MipImage>
EncodeASTCMip(const MipImage& srcMip,
    astcenc_context& context,
    const std::string_view texturePath,
    const uint32_t mipLevel)
{
    MLG_CHECK(srcMip.GetFormat() == TextureFormat::GetRGBA(), "Source mip must be in RGBA format");

    constexpr uint32_t kImageDepth = 1;
    constexpr uint32_t kThreadIndex = 0;
    constexpr astcenc_swizzle kSwizzle //
        {
            .r = ASTCENC_SWZ_R,
            .g = ASTCENC_SWZ_G,
            .b = ASTCENC_SWZ_B,
            .a = ASTCENC_SWZ_A,
        };
    const TextureFormat& astcFormat = TextureFormat::GetASTC();

    MLG_INFO("Encoding {} -> ASTC (mip {})...", texturePath, mipLevel);

    std::vector<std::byte> outputBytes(
        astcFormat.GetBlockAlignedSizeofData(srcMip.GetWidth(), srcMip.GetHeight()));

    void* pixels = srcMip.GetBytes().data();
    astcenc_image image // One slice for a 2D image.
        {
            .dim_x = srcMip.GetWidth(),
            .dim_y = srcMip.GetHeight(),
            .dim_z = kImageDepth,
            .data_type = ASTCENC_TYPE_U8,
            // Source pixels are passed as mutable because the API also does decompression.
            // Compression does not modify them.
            .data = &pixels,
        };

    void* output = outputBytes.data();
    // The encoder repeats edge pixels for partial blocks.
    const astcenc_error status = astcenc_compress_image(&context,
        &image,
        &kSwizzle,
        static_cast<uint8_t*>(output),
        outputBytes.size(),
        kThreadIndex);

    MLG_CHECK(status == ASTCENC_SUCCESS,
        "ASTC encoding failed: {}",
        astcenc_get_error_string(status));

    return MipImage::Create(srcMip.GetWidth(),
        srcMip.GetHeight(),
        astcFormat,
        std::move(outputBytes));
}

} // namespace

Result<MipMapChain>
EncodeBC7(const MipMapChain& mipChain, const std::string_view texturePath)
{
    MLG_CHECK(mipChain.GetFormat() == TextureFormat::GetRGBA(),
        "Only RGBA format is supported for BC7 encoding");

    const TextureFormat& format = TextureFormat::GetBC7();

    // The base mip image must be block aligned for BC7 encoding

    const std::span<const MipImage> mipMaps = mipChain.GetMipMaps();

    const MipImage& baseMip = mipMaps.front();
    const size_t blockAlignedWidth = format.GetBlockAlignedWidth(baseMip.GetWidth());
    const size_t blockAlignedHeight = format.GetBlockAlignedHeight(baseMip.GetHeight());

    MLG_CHECK(baseMip.GetWidth() == blockAlignedWidth && baseMip.GetHeight() == blockAlignedHeight,
        "Base mip image must be block aligned for BC7 encoding");

    BoundedVector<MipImage> mipmaps(mipMaps.size());

    for(size_t i = 0; i < mipMaps.size(); ++i)
    {
        auto encodeResult = EncodeBC7Mip(mipMaps[i], texturePath, static_cast<uint32_t>(i));
        MLG_CHECK(encodeResult, "Failed to encode BC7 mip level for texture: {}", texturePath);
        mipmaps.emplace_back(std::move(*encodeResult));
    }

    return MipMapChain::Create(std::move(mipmaps));
}

Result<MipMapChain>
EncodeASTC(const MipMapChain& mipChain, const std::string_view texturePath)
{
    MLG_CHECK(mipChain.GetFormat() == TextureFormat::GetRGBA(),
        "Only RGBA format is supported for ASTC encoding");

    const TextureFormat& format = TextureFormat::GetASTC();

    // The base mip image must be block aligned for ASTC encoding

    const std::span<const MipImage> mipMaps = mipChain.GetMipMaps();

    const MipImage& baseMip = mipMaps.front();
    const size_t blockAlignedWidth = format.GetBlockAlignedWidth(baseMip.GetWidth());
    const size_t blockAlignedHeight = format.GetBlockAlignedHeight(baseMip.GetHeight());

    MLG_CHECK(baseMip.GetWidth() == blockAlignedWidth && baseMip.GetHeight() == blockAlignedHeight,
        "Base mip image must be block aligned for ASTC encoding");

    const float kQuality = ASTCENC_PRE_MEDIUM;
    constexpr uint32_t kBlockDepth = 1;
    constexpr uint32_t kThreadCount = 1;
    constexpr uint32_t kFlags = 0;

    astcenc_config config{};
    astcenc_error status = astcenc_config_init(ASTCENC_PRF_LDR_SRGB,
        format.GetBlockDimension(),
        format.GetBlockDimension(),
        kBlockDepth,
        kQuality,
        kFlags,
        &config);

    MLG_CHECK(status == ASTCENC_SUCCESS,
        "Failed to configure ASTC encoder: {}",
        astcenc_get_error_string(status));

    astcenc_context* contextPtr = nullptr;
    status = astcenc_context_alloc(&config, kThreadCount, &contextPtr, nullptr);
    const std::unique_ptr<astcenc_context, AstcContextDeleter> context(contextPtr);

    MLG_CHECK(status == ASTCENC_SUCCESS,
        "Failed to create ASTC context: {}",
        astcenc_get_error_string(status));

    BoundedVector<MipImage> mipmaps(mipMaps.size());

    // Single-threaded contexts can be reused without resetting between images.
    for(size_t i = 0; i < mipMaps.size(); ++i)
    {
        auto encodeResult =
            EncodeASTCMip(mipMaps[i], *context, texturePath, static_cast<uint32_t>(i));
        MLG_CHECK(encodeResult, "Failed to encode ASTC mip level for texture: {}", texturePath);
        mipmaps.emplace_back(std::move(*encodeResult));
    }

    return MipMapChain::Create(std::move(mipmaps));
}
