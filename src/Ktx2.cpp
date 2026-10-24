#include "Ktx2.h"

#include <algorithm>
#include <cstring>

namespace
{
bool
ContainsRange(const std::span<const std::byte> buffer, const uint64_t offset, const uint64_t length)
{
    return offset <= buffer.size() && length <= buffer.size() - offset;
}

bool
IsKtx2(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(ktx2::kIdentifier))
    {
        return false;
    }

    return std::memcmp(buffer.data(), &ktx2::kIdentifier.Bytes[0], sizeof(ktx2::kIdentifier)) == 0;
}
} // namespace

Result<Ktx2TextureInfo>
Ktx2TextureInfo::GetInfo(const std::span<const std::byte> buffer)
{
    MLG_CHECK(IsKtx2(buffer));
    MLG_CHECK(buffer.size() >= sizeof(ktx2::FileHeader), "Truncated KTX2 header");
    ktx2::FileHeader fileHeader{};
    std::memcpy(&fileHeader, buffer.data(), sizeof(fileHeader));
    MLG_CHECK(fileHeader.VkFormat == ktx2::kVkFormatBc7Srgb
            || fileHeader.VkFormat == ktx2::kVkFormatAstc4x4Srgb
            || fileHeader.VkFormat == ktx2::kVkFormatRgba8Srgb,
        "KTX2 loader requires BC7, ASTC 4x4, or RGBA8 sRGB data");
    MLG_CHECK(fileHeader.TypeSize == 1, "Invalid KTX2 type size");
    MLG_CHECK(fileHeader.PixelDepth == 0 && fileHeader.LayerCount == 0 && fileHeader.FaceCount == 1,
        "KTX2 loader requires a non-array 2D texture");
    MLG_CHECK(fileHeader.SupercompressionScheme == 0, "KTX2 supercompression is unsupported");
    MLG_CHECK(fileHeader.GlobalDataOffset == 0 && fileHeader.GlobalDataSize == 0,
        "Unexpected KTX2 supercompression global data");

    TextureFormat format = TextureFormat::GetRGBA();
    if(fileHeader.VkFormat == ktx2::kVkFormatBc7Srgb)
    {
        format = TextureFormat::GetBC7();
    }
    else if(fileHeader.VkFormat == ktx2::kVkFormatAstc4x4Srgb)
    {
        format = TextureFormat::GetASTC();
    }

    const Ktx2TextureInfo info //
        {
            .Width = fileHeader.PixelWidth,
            .Height = fileHeader.PixelHeight,
            .MipCount = fileHeader.LevelCount,
            .Format = format,
        };
    MLG_CHECK(info.Format.CheckDimensions(info.Width, info.Height, info.MipCount));

    const size_t mipLevelIndexEnd =
        sizeof(ktx2::FileHeader) + (sizeof(ktx2::LevelIndexEntry) * info.MipCount);
    MLG_CHECK(buffer.size() >= mipLevelIndexEnd, "Truncated KTX2 level index");

    const uint64_t descriptorOffset = fileHeader.DescriptorOffset;
    const uint64_t descriptorSize = fileHeader.DescriptorSize;
    const size_t expectedDescriptorSize = format.IsRgba()
        ? ktx2::kRgbaDescriptorByteCount
        : ktx2::kSingleSampleDescriptorByteCount;
    MLG_CHECK(descriptorSize == expectedDescriptorSize, "Unexpected KTX2 descriptor size");
    MLG_CHECK(descriptorOffset >= mipLevelIndexEnd
            && descriptorOffset % ktx2::kUint32ByteCount == 0
            && ContainsRange(buffer, descriptorOffset, descriptorSize),
        "Invalid KTX2 descriptor range");
    ktx2::DataFormatDescriptorHeader descriptor{};
    std::memcpy(&descriptor,
        buffer.subspan(static_cast<size_t>(descriptorOffset)).data(),
        sizeof(descriptor));
    const uint32_t expectedVersionAndSize = format.IsRgba()
        ? ktx2::kRgbaDescriptorVersionAndSize
        : ktx2::kDescriptorVersionAndSize;
    MLG_CHECK(descriptor.TotalSize == descriptorSize
            && descriptor.VendorAndDescriptorType == 0
            && descriptor.VersionAndBlockSize == expectedVersionAndSize,
        "Unsupported KTX2 descriptor");
    MLG_CHECK(descriptor.BytesPlane0To3 == format.GetBytesPerBlock()
            && descriptor.BytesPlane4To7 == 0,
        "KTX2 descriptor does not match the texture format's byte size");

    const size_t samplesOffset = static_cast<size_t>(descriptorOffset) + sizeof(descriptor);
    if(format.IsRgba())
    {
        MLG_CHECK(descriptor.ColorModelPrimariesTransferFlags == ktx2::kRgba8SrgbStraightAlphaBt709
                && descriptor.TexelBlockDimensions == ktx2::kRgba8TexelBlockDimensions,
            "KTX2 descriptor does not match RGBA8 sRGB color properties");
        constexpr uint32_t kExpectedChannels[] = {
            ktx2::kRgba8RedSampleBitOffsetLengthChannel,
            ktx2::kRgba8GreenSampleBitOffsetLengthChannel,
            ktx2::kRgba8BlueSampleBitOffsetLengthChannel,
            ktx2::kRgba8AlphaSampleBitOffsetLengthChannel,
        };
        for(size_t channel = 0; channel < ktx2::kRgbaSampleCount; ++channel)
        {
            ktx2::DataFormatSample sample{};
            std::memcpy(&sample,
                buffer.subspan(samplesOffset + (channel * sizeof(sample))).data(),
                sizeof(sample));
            MLG_CHECK(sample.BitOffsetLengthChannel == kExpectedChannels[channel]
                    && sample.Position == 0
                    && sample.Lower == 0
                    && sample.Upper == ktx2::kRgba8SampleUpper,
                "Invalid KTX2 RGBA sample {}",
                channel);
        }
    }
    else
    {
        ktx2::DataFormatSample sample{};
        std::memcpy(&sample, buffer.subspan(samplesOffset).data(), sizeof(sample));
        MLG_CHECK(descriptor.ColorModelPrimariesTransferFlags
                    == (info.Format.IsBC7() ? ktx2::kBc7SrgbStraightAlphaBt709
                                            : ktx2::kAstcSrgbStraightAlphaBt709)
                && descriptor.TexelBlockDimensions
                    == (info.Format.IsBC7() ? ktx2::kBc7TexelBlockDimensions
                                            : ktx2::kAstc4x4TexelBlockDimensions)
                && sample.BitOffsetLengthChannel
                    == (info.Format.IsBC7() ? ktx2::kBc7SampleBitOffsetLengthChannel
                                            : ktx2::kAstcSampleBitOffsetLengthChannel)
                && sample.Position == 0
                && sample.Lower == 0
                && sample.Upper == ktx2::kUnormSampleUpper,
            "KTX2 descriptor does not match the texture format or supported color properties");
    }

    uint64_t metadataEnd = descriptorOffset + descriptorSize;
    const uint64_t keyValueOffset = fileHeader.KeyValueOffset;
    const uint64_t keyValueSize = fileHeader.KeyValueSize;
    if(keyValueSize == 0)
    {
        MLG_CHECK(keyValueOffset == 0, "Invalid empty KTX2 key/value range");
    }
    else
    {
        MLG_CHECK(keyValueOffset >= metadataEnd
                && keyValueOffset % ktx2::kUint32ByteCount == 0
                && keyValueSize % ktx2::kUint32ByteCount == 0
                && ContainsRange(buffer, keyValueOffset, keyValueSize),
            "Invalid KTX2 key/value range");
        metadataEnd = keyValueOffset + keyValueSize;
    }

    // Mip payloads run from smallest to largest. Reject overlaps with metadata or other mips.
    uint64_t previousEnd = metadataEnd;
    for(uint32_t mip = info.MipCount; mip > 0; --mip)
    {
        const size_t entry = sizeof(ktx2::FileHeader) + (sizeof(ktx2::LevelIndexEntry) * (mip - 1));
        ktx2::LevelIndexEntry level{};
        std::memcpy(&level, buffer.subspan(entry).data(), sizeof(level));
        const uint32_t width = std::max(info.Width >> (mip - 1), 1u);
        const uint32_t height = std::max(info.Height >> (mip - 1), 1u);
        MLG_CHECK(level.ByteLength == info.Format.GetBlockAlignedSizeofData(width, height)
                && level.UncompressedByteLength == level.ByteLength,
            "Invalid KTX2 mip {} size: expected {}, got {}",
            mip - 1,
            info.Format.GetBlockAlignedSizeofData(width, height),
            level.ByteLength);
        MLG_CHECK(level.ByteOffset >= previousEnd
                && level.ByteOffset % info.Format.GetBytesPerBlock() == 0
                && ContainsRange(buffer, level.ByteOffset, level.ByteLength),
            "Invalid KTX2 mip {} range",
            mip - 1);
        previousEnd = level.ByteOffset + level.ByteLength;
    }
    return info;
}

Result<std::span<const std::byte>>
Ktx2Container::GetMipBytes(const uint32_t mipLevel) const
{
    MLG_CHECK(mipLevel < m_Info.MipCount,
        "KTX2 mip index out of range: {} >= {}",
        mipLevel,
        m_Info.MipCount);

    const size_t entry = sizeof(ktx2::FileHeader) + (sizeof(ktx2::LevelIndexEntry) * mipLevel);
    const std::span<const std::byte> bufferSpan = m_Buffer;
    ktx2::LevelIndexEntry level{};
    std::memcpy(&level, bufferSpan.subspan(entry).data(), sizeof(level));
    return std::span(bufferSpan.subspan(static_cast<size_t>(level.ByteOffset),
        static_cast<size_t>(level.ByteLength)));
}
