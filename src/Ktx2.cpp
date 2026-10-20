#include "Ktx2.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <type_traits>

namespace
{
constexpr uint8_t kIdentifier[] //
    { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
constexpr size_t kFileHeaderByteCount = 80;
constexpr size_t kGlobalDataOffsetFieldOffset = 64;
constexpr size_t kLevelIndexEntryByteCount = 24;
constexpr size_t kDescriptorByteCount = 44;
constexpr size_t kUint32ByteCount = sizeof(uint32_t);
constexpr uint32_t kDescriptorVersionAndSize = 0x00280002;
constexpr uint32_t kBc7SrgbStraightAlphaBt709 = 0x00020186;
constexpr uint32_t kBc7TexelBlockDimensions = 0x00000303;
constexpr uint32_t kBc7SampleBitOffsetLengthChannel = 0x007F0000;
constexpr uint32_t kUnormSampleUpper = 0xFFFFFFFF;

// On-disk layouts. Copy into local objects so input buffers need not be aligned.
struct FileHeader
{
    uint8_t Identifier[sizeof(kIdentifier)];
    uint32_t VkFormat;
    uint32_t TypeSize;
    uint32_t PixelWidth;
    uint32_t PixelHeight;
    uint32_t PixelDepth;
    uint32_t LayerCount;
    uint32_t FaceCount;
    uint32_t LevelCount;
    uint32_t SupercompressionScheme;
    uint32_t DescriptorOffset;
    uint32_t DescriptorSize;
    uint32_t KeyValueOffset;
    uint32_t KeyValueSize;
    // Supercompression global data.
    uint64_t GlobalDataOffset;
    uint64_t GlobalDataSize;
};

struct LevelIndexEntry
{
    uint64_t ByteOffset;
    uint64_t ByteLength;
    uint64_t UncompressedByteLength;
};

// Data format descriptor with one basic block and one sample, as used by BC7.
struct DataFormatDescriptor
{
    uint32_t TotalSize;
    uint32_t VendorAndDescriptorType;
    uint32_t VersionAndBlockSize;
    uint32_t ColorModelPrimariesTransferFlags;
    uint32_t TexelBlockDimensions;
    uint32_t BytesPlane0To3;
    uint32_t BytesPlane4To7;
    uint32_t SampleBitOffsetLengthChannel;
    uint32_t SamplePosition;
    uint32_t SampleLower;
    uint32_t SampleUpper;
};

static_assert(std::endian::native == std::endian::little,
    "KTX2 struct reads require a little-endian host");
static_assert(std::is_trivially_copyable_v<FileHeader>);
static_assert(std::is_trivially_copyable_v<LevelIndexEntry>);
static_assert(std::is_trivially_copyable_v<DataFormatDescriptor>);
static_assert(sizeof(FileHeader) == kFileHeaderByteCount);
static_assert(offsetof(FileHeader, VkFormat) == sizeof(kIdentifier));
static_assert(offsetof(FileHeader, GlobalDataOffset) == kGlobalDataOffsetFieldOffset);
static_assert(sizeof(LevelIndexEntry) == kLevelIndexEntryByteCount);
static_assert(sizeof(DataFormatDescriptor) == kDescriptorByteCount);

constexpr uint32_t kVkFormatBc7Srgb = 146;

bool
ContainsRange(const std::span<const std::byte> buffer, const uint64_t offset, const uint64_t length)
{
    return offset <= buffer.size() && length <= buffer.size() - offset;
}
} // namespace

bool
Ktx2Header::IsKtx2(const std::span<const std::byte> buffer)
{
    if(buffer.size() < sizeof(kIdentifier))
    {
        return false;
    }

    return std::memcmp(buffer.data(), &kIdentifier[0], sizeof(kIdentifier)) == 0;
}

Result<Ktx2Header>
Ktx2Header::GetHeader(const std::span<const std::byte> buffer)
{
    MLG_CHECK(IsKtx2(buffer), "Invalid KTX2 identifier");
    MLG_CHECK(buffer.size() >= sizeof(FileHeader), "Truncated KTX2 header");
    FileHeader fileHeader{};
    std::memcpy(&fileHeader, buffer.data(), sizeof(fileHeader));
    MLG_CHECK(fileHeader.VkFormat == kVkFormatBc7Srgb, "KTX2 loader requires BC7 sRGB data");
    MLG_CHECK(fileHeader.TypeSize == 1, "Invalid KTX2 type size");
    MLG_CHECK(fileHeader.PixelDepth == 0 && fileHeader.LayerCount == 0 && fileHeader.FaceCount == 1,
        "KTX2 loader requires a non-array 2D texture");
    MLG_CHECK(fileHeader.SupercompressionScheme == 0, "KTX2 supercompression is unsupported");
    MLG_CHECK(fileHeader.GlobalDataOffset == 0 && fileHeader.GlobalDataSize == 0,
        "Unexpected KTX2 supercompression global data");

    const Ktx2Header header //
        {
            .Width = fileHeader.PixelWidth,
            .Height = fileHeader.PixelHeight,
            .MipCount = fileHeader.LevelCount,
            .Format = TextureFormat::GetBC7(),
        };
    MLG_CHECK(header.Format.CheckDimensions(header.Width, header.Height, header.MipCount));

    const size_t mipLevelIndexEnd =
        sizeof(FileHeader) + (sizeof(LevelIndexEntry) * header.MipCount);
    MLG_CHECK(buffer.size() >= mipLevelIndexEnd, "Truncated KTX2 level index");

    const uint64_t descriptorOffset = fileHeader.DescriptorOffset;
    const uint64_t descriptorSize = fileHeader.DescriptorSize;
    MLG_CHECK(descriptorSize == sizeof(DataFormatDescriptor),
        "Unexpected KTX2 descriptor size");
    MLG_CHECK(descriptorOffset >= mipLevelIndexEnd
            && descriptorOffset % kUint32ByteCount == 0
            && ContainsRange(buffer, descriptorOffset, descriptorSize),
        "Invalid KTX2 descriptor range");
    DataFormatDescriptor descriptor{};
    std::memcpy(&descriptor,
        buffer.subspan(static_cast<size_t>(descriptorOffset)).data(),
        sizeof(descriptor));
    MLG_CHECK(descriptor.TotalSize == descriptorSize
            && descriptor.VendorAndDescriptorType == 0
            && descriptor.VersionAndBlockSize == kDescriptorVersionAndSize,
        "Unsupported KTX2 descriptor");
    MLG_CHECK(descriptor.ColorModelPrimariesTransferFlags == kBc7SrgbStraightAlphaBt709
            && descriptor.TexelBlockDimensions == kBc7TexelBlockDimensions
            && descriptor.BytesPlane0To3 == header.Format.GetBytesPerBlock()
            && descriptor.BytesPlane4To7 == 0
            && descriptor.SampleBitOffsetLengthChannel == kBc7SampleBitOffsetLengthChannel
            && descriptor.SamplePosition == 0
            && descriptor.SampleLower == 0
            && descriptor.SampleUpper == kUnormSampleUpper,
        "KTX2 descriptor does not match supported BC7 sRGB data");

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
                && keyValueOffset % kUint32ByteCount == 0
                && keyValueSize % kUint32ByteCount == 0
                && ContainsRange(buffer, keyValueOffset, keyValueSize),
            "Invalid KTX2 key/value range");
        metadataEnd = keyValueOffset + keyValueSize;
    }

    // Mip payloads run from smallest to largest. Reject overlaps with metadata or other mips.
    uint64_t previousEnd = metadataEnd;
    for(uint32_t mip = header.MipCount; mip > 0; --mip)
    {
        const size_t entry = sizeof(FileHeader) + (sizeof(LevelIndexEntry) * (mip - 1));
        LevelIndexEntry level{};
        std::memcpy(&level, buffer.subspan(entry).data(), sizeof(level));
        const uint32_t width = std::max(header.Width >> (mip - 1), 1u);
        const uint32_t height = std::max(header.Height >> (mip - 1), 1u);
        MLG_CHECK(level.ByteLength == header.Format.GetBlockAlignedSizeofData(width, height)
                && level.UncompressedByteLength == level.ByteLength,
            "Invalid KTX2 mip {} size: expected {}, got {}",
            mip - 1,
            header.Format.GetBlockAlignedSizeofData(width, height),
            level.ByteLength);
        MLG_CHECK(level.ByteOffset >= previousEnd
                && level.ByteOffset % header.Format.GetBytesPerBlock() == 0
                && ContainsRange(buffer, level.ByteOffset, level.ByteLength),
            "Invalid KTX2 mip {} range",
            mip - 1);
        previousEnd = level.ByteOffset + level.ByteLength;
    }
    return header;
}

Result<std::span<const std::byte>>
Ktx2Header::GetMipBytes(const std::span<const std::byte> buffer, const uint32_t mipLevel)
{
    const auto header = GetHeader(buffer);
    MLG_CHECK(header);
    MLG_CHECK(mipLevel < header->MipCount, "KTX2 mip index out of range: {} >= {}", mipLevel, header->MipCount);
    const size_t entry = sizeof(FileHeader) + (sizeof(LevelIndexEntry) * mipLevel);
    LevelIndexEntry level{};
    std::memcpy(&level, buffer.subspan(entry).data(), sizeof(level));
    return buffer.subspan(static_cast<size_t>(level.ByteOffset),
        static_cast<size_t>(level.ByteLength));
}
