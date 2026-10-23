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
            || fileHeader.VkFormat == ktx2::kVkFormatAstc4x4Srgb,
        "KTX2 loader requires BC7 or ASTC 4x4 sRGB data");
    MLG_CHECK(fileHeader.TypeSize == 1, "Invalid KTX2 type size");
    MLG_CHECK(fileHeader.PixelDepth == 0 && fileHeader.LayerCount == 0 && fileHeader.FaceCount == 1,
        "KTX2 loader requires a non-array 2D texture");
    MLG_CHECK(fileHeader.SupercompressionScheme == 0, "KTX2 supercompression is unsupported");
    MLG_CHECK(fileHeader.GlobalDataOffset == 0 && fileHeader.GlobalDataSize == 0,
        "Unexpected KTX2 supercompression global data");

    const Ktx2TextureInfo info //
        {
            .Width = fileHeader.PixelWidth,
            .Height = fileHeader.PixelHeight,
            .MipCount = fileHeader.LevelCount,
            .Format = fileHeader.VkFormat == ktx2::kVkFormatBc7Srgb ? TextureFormat::GetBC7()
                                                                    : TextureFormat::GetASTC(),
        };
    MLG_CHECK(info.Format.CheckDimensions(info.Width, info.Height, info.MipCount));

    const size_t mipLevelIndexEnd =
        sizeof(ktx2::FileHeader) + (sizeof(ktx2::LevelIndexEntry) * info.MipCount);
    MLG_CHECK(buffer.size() >= mipLevelIndexEnd, "Truncated KTX2 level index");

    const uint64_t descriptorOffset = fileHeader.DescriptorOffset;
    const uint64_t descriptorSize = fileHeader.DescriptorSize;
    MLG_CHECK(descriptorSize == sizeof(ktx2::DataFormatDescriptor),
        "Unexpected KTX2 descriptor size");
    MLG_CHECK(descriptorOffset >= mipLevelIndexEnd
            && descriptorOffset % ktx2::kUint32ByteCount == 0
            && ContainsRange(buffer, descriptorOffset, descriptorSize),
        "Invalid KTX2 descriptor range");
    ktx2::DataFormatDescriptor descriptor{};
    std::memcpy(&descriptor,
        buffer.subspan(static_cast<size_t>(descriptorOffset)).data(),
        sizeof(descriptor));
    MLG_CHECK(descriptor.TotalSize == descriptorSize
            && descriptor.VendorAndDescriptorType == 0
            && descriptor.VersionAndBlockSize == ktx2::kDescriptorVersionAndSize,
        "Unsupported KTX2 descriptor");
    MLG_CHECK(descriptor.ColorModelPrimariesTransferFlags
                == (info.Format.IsBC7() ? ktx2::kBc7SrgbStraightAlphaBt709
                                        : ktx2::kAstcSrgbStraightAlphaBt709)
            && descriptor.TexelBlockDimensions
                == (info.Format.IsBC7() ? ktx2::kBc7TexelBlockDimensions
                                        : ktx2::kAstc4x4TexelBlockDimensions)
            && descriptor.BytesPlane0To3 == info.Format.GetBytesPerBlock()
            && descriptor.BytesPlane4To7 == 0
            && descriptor.SampleBitOffsetLengthChannel
                == (info.Format.IsBC7() ? ktx2::kBc7SampleBitOffsetLengthChannel
                                        : ktx2::kAstcSampleBitOffsetLengthChannel)
            && descriptor.SamplePosition == 0
            && descriptor.SampleLower == 0
            && descriptor.SampleUpper == ktx2::kUnormSampleUpper,
        "KTX2 descriptor does not match the texture format or supported color properties");

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
